/*++

Copyright (c) Microsoft Corporation All Rights Reserved

Module Name:

    vcable.cpp

Abstract:

    Implementation of SYSVAD data saving class.

    To save the playback data to disk, this class maintains a circular data
    buffer, associated frame structures and worker items to save frames to
    disk.
    Each frame structure represents a portion of buffer. When that portion
    of frame is full, a workitem is scheduled to save it to disk.

--*/
#pragma warning (disable : 4127)
#pragma warning (disable : 26165)

#include <sysvad.h>
#include "vcable.h"
#include <ntstrsafe.h>   // This is for using RtlStringcbPrintf

#define VCABLE_POOLTAG1 '1DVS'
#define VCABLE_POOLTAG2 '2DVS'
#define VCABLE_POOLTAG4 '4DVS'

//=============================================================================
// Defines
//=============================================================================
#define RIFF_TAG                    0x46464952;
#define WAVE_TAG                    0x45564157;
#define FMT__TAG                    0x20746D66;
#define DATA_TAG                    0x61746164;

#define DEFAULT_FRAME_COUNT         4
#define DEFAULT_FRAME_SIZE          PAGE_SIZE * 4
#define DEFAULT_BUFFER_SIZE         DEFAULT_FRAME_SIZE * DEFAULT_FRAME_COUNT

#define MAX_WORKER_ITEM_COUNT       15

//=============================================================================
// Statics
//=============================================================================
ULONG       CVCable::m_ulStreamId = 0;
CVCable*    CVCable::m_pInstance = nullptr;


#pragma code_seg("PAGE")
//=============================================================================
// CVCable
//=============================================================================

//=============================================================================
CVCable::CVCable()
:   m_pDataBuffer(NULL),
    m_ulBufferSize(DEFAULT_BUFFER_SIZE * 256),
    m_fWriteDisabled(FALSE),
    m_bCaptureInitialized(FALSE),
    m_Initialized(FALSE),
    m_bRenderInitialized(FALSE)
{
    PAGED_CODE();

} // CVCable

//=============================================================================
CVCable::~CVCable()
{
    PAGED_CODE();

    DPF_ENTER(("[CVCable::~CVCable]"));

    // Update the wave header in data file with real file size.
    //
    if (m_pDataBuffer)
    {
        ExFreePoolWithTag(m_pDataBuffer, VCABLE_POOLTAG4);
        m_pDataBuffer = NULL;
    }
} // CVCable


//=============================================================================
// getCable: implements a quasi-singleton pattern. The intent is for there to
//           be only one virtual cable object for a pair of input and output
//           devices.
//=============================================================================
CVCable* CVCable::getCable( _In_ BOOL capture )
{
    PAGED_CODE();

    if (m_pInstance == nullptr)
    {
        m_pInstance = new (NonPagedPoolNx, VCABLE_POOLTAG2) CVCable();
    }

    if (capture)
    {
        if (!m_pInstance->isCapture()) m_pInstance->setCapture();
        //else return nullptr;
    }
    else
    {
        if (!m_pInstance->isRender()) m_pInstance->setRender();
        //else return nullptr;
    }
    return m_pInstance;
}

//=============================================================================
void
CVCable::Disable( _In_ BOOL fDisable )
{
    PAGED_CODE();

    m_fWriteDisabled = fDisable;
} // Disable

//=============================================================================
NTSTATUS
CVCable::Initialize()
{
    PAGED_CODE();

    NTSTATUS    ntStatus = STATUS_SUCCESS;

    DPF_ENTER(("[CVCable::Initialize]"));

    m_ulStreamId++;

    if (!m_Initialized)
    {
        m_Initialized = true;

        // Allocate memory for data buffer.
        //
        m_pDataBuffer = (PBYTE)ExAllocatePoolWithTag(NonPagedPoolNx,
                                                     m_ulBufferSize,
                                                     VCABLE_POOLTAG4);
        if (!m_pDataBuffer)
        {
            DPF(D_TERSE, ("[Could not allocate memory for Saving Data]"));
            ntStatus = STATUS_INSUFFICIENT_RESOURCES;
        } else
        {
            // Set both indices to the start of the buffer
            m_ulWriteOffset = m_ulReadOffset = 0;
            RtlZeroMemory(m_pDataBuffer, m_ulBufferSize);
        }

        // Initialize the spinlock to synchronize access to the buffer
        //
        KeInitializeSpinLock(&m_FrameInUseSpinLock);
    }

    return ntStatus;
} // Initialize

//=============================================================================
void
CVCable::ReadData( _Inout_updates_bytes_all_(ulByteCount)  PBYTE   pBuffer,
                   _In_                                    ULONG   ulByteCount)
{
    UNREFERENCED_PARAMETER(pBuffer);
    UNREFERENCED_PARAMETER(ulByteCount);

    PAGED_CODE();

    // Not implemented yet.
} // ReadData

#pragma code_seg()
//=============================================================================
// LoopData: returns looped render data to miniport driver
//
// Parameters: pBuffer     - buffer to receive LPCM data
//             ulByteCount - number of bytes of data to return in buffer
//=============================================================================
void CVCable::LoopData(_Out_writes_bytes_(ulByteCount) PBYTE   pBuffer,
                       _In_                            ULONG   ulByteCount)
{
    ASSERT(pBuffer);
    ASSERT(m_pDataBuffer);
    //ASSERT(ulByteCount < m_ulBufferSize);

    DPF_ENTER(("[CVCable::LoopData ulByteCount=%lu]", ulByteCount));

    // if muted, we deliver silence.
    if (m_Mute)
    {
        RtlZeroMemory(pBuffer, ulByteCount);
    }
    else if (ulByteCount > 0)
    {
        ULONG   ulLength;
        ULONG   ulCopyBytes;
        KIRQL   oldIrql;

        // Lock access to buffer
        KeAcquireSpinLock(&m_FrameInUseSpinLock, &oldIrql);

        if (m_ulReadOffset <= m_ulWriteOffset)
        {
            // We can take a contiguous block of data from the internal buffer
            ulLength = m_ulWriteOffset - m_ulReadOffset;
            ulCopyBytes = ulLength > ulByteCount ? ulByteCount : ulLength;

            DPF_ENTER(("[CVCable::LoopData m_ulReadOffset=%lu m_ulWriteOffset=%lu"
                       " ulLength=%lu ulCopyBytes=%lu]", 
                       m_ulReadOffset, m_ulWriteOffset, ulLength, ulCopyBytes));

            if (ulCopyBytes > 0) RtlCopyMemory(pBuffer, m_pDataBuffer + m_ulReadOffset, ulCopyBytes);
            m_ulReadOffset += ulCopyBytes;
            if (m_ulReadOffset == m_ulBufferSize) m_ulReadOffset = 0;
            ulByteCount -= ulCopyBytes;
            pBuffer += ulCopyBytes;
        }
        else
        {
            // This might have to be handled in two chunks
            ulLength = m_ulBufferSize - m_ulReadOffset;
            ulCopyBytes = ulLength > ulByteCount ? ulByteCount : ulLength;

            RtlCopyMemory(pBuffer, m_pDataBuffer + m_ulReadOffset, ulCopyBytes);
            m_ulReadOffset += ulCopyBytes;
            if (m_ulReadOffset == m_ulBufferSize) m_ulReadOffset = 0;
            ulByteCount -= ulCopyBytes;
            pBuffer += ulCopyBytes;
            if (ulByteCount > 0)
            {
                ulCopyBytes = m_ulWriteOffset > ulByteCount ? ulByteCount : m_ulWriteOffset;
                RtlCopyMemory(pBuffer, m_pDataBuffer + m_ulReadOffset, ulCopyBytes);
                m_ulReadOffset += ulCopyBytes;
                ulByteCount -= ulCopyBytes;
                pBuffer += ulCopyBytes;
           }
        }
        if (ulByteCount > 0)
        {
            // Add silence if there is not enough pending data
            RtlZeroMemory(pBuffer, ulByteCount);
        }

        KeReleaseSpinLock(&m_FrameInUseSpinLock, oldIrql);
    }
}

//=============================================================================
// WriteData: receives render data from miniport driver
//
// Parameters: pBuffer     - buffer containing LPCM data
//             ulByteCount - number of bytes of data in buffer
//=============================================================================
void
CVCable::WriteData( _In_reads_bytes_(ulByteCount)   PBYTE   pBuffer,
                    _In_                            ULONG   ulByteCount)
{
    ASSERT(pBuffer);
    ASSERT(m_pDataBuffer);
    //ASSERT(ulByteCount < m_ulBufferSize);

    KIRQL   oldIrql;

    // If stream writing is disabled, then exit (throw away the data).
    if (m_fWriteDisabled) return;

    DPF_ENTER(("[CVCable::WriteData ulByteCount=%lu]", ulByteCount));

    if (ulByteCount > 0)
    {

        // Don't attempt to write more than the loobback buffer can hold. This 
        // logic makes no attempt to discard the beginning bytes in favour 
        // of the later bytes. Data will be dropped in either case, and this 
        // is easier to code (and faster).
        if (ulByteCount >= m_ulBufferSize)
        {
            ulByteCount = m_ulBufferSize - 1;
        }

        bool    wrapped = false;
        ULONG   ulWriteBytes = ulByteCount;
        ULONG   ulOldWriteOffset = m_ulWriteOffset;

        if( (m_ulBufferSize - m_ulWriteOffset) < ulWriteBytes )
        {
            // Data is split. Calculate space to the end of the buffer
            ulWriteBytes = m_ulBufferSize - m_ulWriteOffset;
        }

        // Lock access to buffer
        KeAcquireSpinLock(&m_FrameInUseSpinLock, &oldIrql);

        RtlCopyMemory(m_pDataBuffer + m_ulWriteOffset, pBuffer, ulWriteBytes);
        m_ulWriteOffset += ulWriteBytes;
        ulByteCount -= ulWriteBytes;
        pBuffer += ulWriteBytes;

        // Loop the buffer, if we reached the end.
        if (m_ulWriteOffset == m_ulBufferSize)
        {
            m_ulWriteOffset = 0;
            wrapped = true;
        }
        if (ulByteCount > 0)
        {
            RtlCopyMemory(m_pDataBuffer + m_ulWriteOffset, pBuffer, ulByteCount);
            m_ulWriteOffset += ulByteCount;
        }

        // Now adjust the read offset if necessary. If the former read offset was
        // passed by the advancing data, that means that the buffer is completely
        // full, in which case the read offset must be advanced to the start of the
        // oldest data in the buffer.
        if (wrapped)
        {
            if ( m_ulReadOffset <= m_ulWriteOffset
                || m_ulReadOffset > ulOldWriteOffset )
            {
                m_ulReadOffset = m_ulWriteOffset + 1;
            }
        }
        else
        {
            if ( (m_ulReadOffset <= m_ulWriteOffset)
                 && (m_ulReadOffset > ulOldWriteOffset) )
            {
                m_ulReadOffset = m_ulWriteOffset + 1;
            }
        }
        if (m_ulReadOffset == m_ulBufferSize) m_ulReadOffset = 0;

        KeReleaseSpinLock(&m_FrameInUseSpinLock, oldIrql);
    }
} // WriteData


/*++

Copyright (c) Microsoft Corporation All Rights Reserved

Module Name:

    vcable.h

Abstract:

    Declaration of SYSVAD data saving class. This class supplies services
to save data to disk.


--*/

#ifndef _SYSVAD_VCABLE_H
#define _SYSVAD_VCABLE_H

//-----------------------------------------------------------------------------
//  Forward declaration
//-----------------------------------------------------------------------------
class CVCable;
typedef CVCable *PCVCable;


//-----------------------------------------------------------------------------
//  Structs
//-----------------------------------------------------------------------------

// Parameter to workitem.
#include <pshpack1.h>
typedef struct _SAVEWORKER_PARAM {
    //PIO_WORKITEM     WorkItem;
    ULONG            ulFrameNo;
    ULONG            ulDataSize;
    PBYTE            pData;
    PCVCable         pVCable;
    KEVENT           EventDone;
} SAVEWORKER_PARAM;
typedef SAVEWORKER_PARAM *PSAVEWORKER_PARAM;
#include <poppack.h>

// wave file header.
#include <pshpack1.h>
typedef struct _OUTPUT_DATA_HEADER
{
    DWORD           dwData;
    DWORD           dwDataLength;
} OUTPUT_DATA_HEADER;
typedef OUTPUT_DATA_HEADER *POUTPUT_DATA_HEADER;

#include <poppack.h>

//-----------------------------------------------------------------------------
//  Classes
//-----------------------------------------------------------------------------

///////////////////////////////////////////////////////////////////////////////
// CVCable
//   Provides a loopback of output to an input stream
//
IO_WORKITEM_ROUTINE SaveFrameWorkerCallback;

class CVCable
{
protected:
    PBYTE                       m_pDataBuffer;      // Data buffer.
    ULONG                       m_ulBufferSize;     // Total buffer size.
    ULONG                       m_ulWriteOffset;    // index in buffer.
    ULONG                       m_ulReadOffset;     // index in buffer.

    KSPIN_LOCK                  m_FrameInUseSpinLock; // Spinlock for synch.

    bool                        m_Mute;

    static ULONG                m_ulStreamId;

    BOOL                        m_fWriteDisabled;

    bool                        m_bCaptureInitialized;
    bool                        m_bRenderInitialized;

public:

    void        Disable( _In_ BOOL fDisable );
    NTSTATUS    Initialize();
    //NTSTATUS    SetMaxWriteSize( _In_  ULONG ulMaxWriteSize );

    void    SetMute(_In_ bool b) { m_Mute = b; }


    bool    isCapture() const              { return m_bCaptureInitialized; }
    void    setCapture(_In_ bool b = true) { m_bCaptureInitialized = b; }
    bool    isRender() const               { return m_bRenderInitialized; }
    void    setRender(_In_ bool b = true)  { m_bRenderInitialized = b; }

    void    ReadData( _Inout_updates_bytes_all_(ulByteCount) PBYTE pBuffer,
                      _In_                                   ULONG ulByteCount);

    void    WriteData(_In_reads_bytes_(ulByteCount)   PBYTE  pBuffer,
                      _In_                            ULONG  ulByteCount);

    void    LoopData( _Out_writes_bytes_(BufferLength) PBYTE pBuffer,
                      _In_                             ULONG ulByteCount);

    static CVCable* getCable( _In_ BOOL capture );

private:
    CVCable();
    ~CVCable();

    bool            m_Initialized;

    static CVCable* m_pInstance;

};
typedef CVCable *PCVCable;

#endif


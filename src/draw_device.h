#pragma once
#include <mfidl.h>

typedef void (*IMAGE_TRANSFORM_FN)(
    BYTE*       pDest,
    LONG        lDestStride,
    const BYTE* pSrc,
    LONG        lSrcStride,
    DWORD       dwWidthInPixels,
    DWORD       dwHeightInPixels
);

class LayeredWindow;

class DrawDevice
{
public:
    void Init(LayeredWindow* layered_win);
    HRESULT SetVideoType(IMFMediaType *pType);
    SIZE FrameSize() const;
    HRESULT DrawFrame(IMFMediaBuffer *pBuffer);

    BOOL IsFormatSupported(REFGUID subtype) const;
    HRESULT GetFormat(DWORD index, GUID *pSubtype) const;

private:
    HRESULT SetConversionFunction(REFGUID subtype);

    LayeredWindow* layered_win_ = nullptr;
    UINT32 m_width = 0;
    UINT32 m_height = 0;
    LONG m_lDefaultStride = 0;
    IMAGE_TRANSFORM_FN m_convertFn = nullptr;
};

class VideoBufferLock
{
public:
    VideoBufferLock(IMFMediaBuffer* pBuffer);
    ~VideoBufferLock();

    HRESULT LockBuffer(
        LONG  lDefaultStride,    // Minimum stride (with no padding).
        DWORD dwHeightInPixels,  // Height of the image, in pixels.
        BYTE** ppbScanLine0,    // Receives a pointer to the start of scan line 0.
        LONG* plStride          // Receives the actual stride.
    );

    void UnlockBuffer();

private:
    IMFMediaBuffer* m_pBuffer = NULL;
    IMF2DBuffer* m_p2DBuffer;
    BOOL m_bLocked = FALSE;
};

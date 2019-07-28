#include "draw_device.h"
#include <mfapi.h>
#include <mferror.h>
#include <d3d9.h>

#include "window.h"
#include "util.h"

void TransformImage_RGB24(
    BYTE*       pDest,
    LONG        lDestStride,
    const BYTE* pSrc,
    LONG        lSrcStride,
    DWORD       dwWidthInPixels,
    DWORD       dwHeightInPixels
);

void TransformImage_RGB32(
    BYTE*       pDest,
    LONG        lDestStride,
    const BYTE* pSrc,
    LONG        lSrcStride,
    DWORD       dwWidthInPixels,
    DWORD       dwHeightInPixels
);

void TransformImage_YUY2(
    BYTE*       pDest,
    LONG        lDestStride,
    const BYTE* pSrc,
    LONG        lSrcStride,
    DWORD       dwWidthInPixels,
    DWORD       dwHeightInPixels
);

void TransformImage_NV12(
    BYTE* pDst, 
    LONG dstStride, 
    const BYTE* pSrc, 
    LONG srcStride,
    DWORD dwWidthInPixels,
    DWORD dwHeightInPixels
);

HRESULT GetDefaultStride(IMFMediaType *pType, LONG *plStride);

inline LONG Width(const RECT& r)
{
    return r.right - r.left;
}

inline LONG Height(const RECT& r)
{
    return r.bottom - r.top;
}

struct ConversionFunction
{
    GUID subtype;
    IMAGE_TRANSFORM_FN xform;
};

ConversionFunction g_FormatConversions[] = {
    { MFVideoFormat_RGB32, TransformImage_RGB32 },
    { MFVideoFormat_RGB24, TransformImage_RGB24 },
    { MFVideoFormat_YUY2,  TransformImage_YUY2  },      
    { MFVideoFormat_NV12,  TransformImage_NV12  }
};

const DWORD g_cFormats = ARRAYSIZE(g_FormatConversions);

HRESULT DrawDevice::GetFormat(DWORD index, GUID *pSubtype) const
{
    if (index < g_cFormats) {
        *pSubtype = g_FormatConversions[index].subtype;
        return S_OK;
    }

    return MF_E_NO_MORE_TYPES;
}

BOOL DrawDevice::IsFormatSupported(REFGUID subtype) const
{
    for (DWORD i = 0; i < g_cFormats; i++) {
        if (subtype == g_FormatConversions[i].subtype)
            return TRUE;
    }

    return FALSE;
}

void DrawDevice::Init(LayeredWindow* layered_win)
{
    layered_win_ = layered_win;
}

HRESULT DrawDevice::SetConversionFunction(REFGUID subtype)
{
    m_convertFn = NULL;

    for (DWORD i = 0; i < g_cFormats; i++) {
        if (g_FormatConversions[i].subtype == subtype) {
            m_convertFn = g_FormatConversions[i].xform;
            return S_OK;
        }
    }

    return MF_E_INVALIDMEDIATYPE;
}

HRESULT DrawDevice::SetVideoType(IMFMediaType *pType)
{
    HRESULT hr = S_OK;

    GUID subtype = {0};
    hr = pType->GetGUID(MF_MT_SUBTYPE, &subtype);
    if (FAILED(hr))
        return hr;

    hr = SetConversionFunction(subtype); 
    if (FAILED(hr))
        return hr;

    hr = MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &m_width, &m_height);
    if (FAILED(hr))
        return hr;

    hr = GetDefaultStride(pType, &m_lDefaultStride);
    if (FAILED(hr))
        return hr;

    return hr;
}

SIZE DrawDevice::FrameSize() const
{
    SIZE s;
    s.cx = (LONG)m_width;
    s.cy = (LONG)m_height;
    return s;
}

HRESULT DrawDevice::DrawFrame(IMFMediaBuffer *pBuffer)
{
    if (m_convertFn == NULL)
        return MF_E_INVALIDREQUEST;

    HRESULT hr = S_OK;
    BYTE* pbScanline0 = NULL;
    LONG lStride = 0;

    VideoBufferLock buffer(pBuffer);
    hr = buffer.LockBuffer(m_lDefaultStride, m_height, &pbScanline0, &lStride);
    if (FAILED(hr))
        return hr;
    
    m_convertFn((BYTE*)layered_win_->BmpBuffer(), layered_win_->BmpStride(),
        pbScanline0, lStride, m_width, m_height);

    layered_win_->OnNewFrame();
    return hr;
}

__forceinline BYTE Clip(int clr)
{
    return (BYTE)(clr < 0 ? 0 : ( clr > 255 ? 255 : clr ));
}

__forceinline RGBQUAD ConvertYCrCbToRGB(
    int y,
    int cr,
    int cb
)
{
    RGBQUAD rgbq;

    int c = y - 16;
    int d = cb - 128;
    int e = cr - 128;

    rgbq.rgbRed =   Clip(( 298 * c           + 409 * e + 128) >> 8);
    rgbq.rgbGreen = Clip(( 298 * c - 100 * d - 208 * e + 128) >> 8);
    rgbq.rgbBlue =  Clip(( 298 * c + 516 * d           + 128) >> 8);
    rgbq.rgbReserved = 255;

    return rgbq;
}

void TransformImage_RGB24(
    BYTE*       pDest,
    LONG        lDestStride,
    const BYTE* pSrc,
    LONG        lSrcStride,
    DWORD       dwWidthInPixels,
    DWORD       dwHeightInPixels
)
{
    for (DWORD y = 0; y < dwHeightInPixels; y++) {
        RGBTRIPLE *pSrcPel = (RGBTRIPLE*)pSrc;
        DWORD *pDestPel = (DWORD*)pDest;

        for (DWORD x = 0; x < dwWidthInPixels; x++) {
            pDestPel[x] = D3DCOLOR_XRGB(
                pSrcPel[x].rgbtRed,
                pSrcPel[x].rgbtGreen,
                pSrcPel[x].rgbtBlue);
        }

        pSrc += lSrcStride;
        pDest += lDestStride;
    }
}

void TransformImage_RGB32(
    BYTE*       pDest,
    LONG        lDestStride,
    const BYTE* pSrc,
    LONG        lSrcStride,
    DWORD       dwWidthInPixels,
    DWORD       dwHeightInPixels
)
{
    MFCopyImage(pDest, lDestStride, pSrc, lSrcStride, dwWidthInPixels * 4, dwHeightInPixels);
}

void TransformImage_YUY2(
    BYTE*       pDest,
    LONG        lDestStride,
    const BYTE* pSrc,
    LONG        lSrcStride,
    DWORD       dwWidthInPixels,
    DWORD       dwHeightInPixels
)
{
    for (DWORD y = 0; y < dwHeightInPixels; y++) {
        RGBQUAD *pDestPel = (RGBQUAD*)pDest;
        WORD    *pSrcPel = (WORD*)pSrc;

        for (DWORD x = 0; x < dwWidthInPixels; x += 2) {
            // Byte order is U0 Y0 V0 Y1
            int y0 = (int)LOBYTE(pSrcPel[x]); 
            int u0 = (int)HIBYTE(pSrcPel[x]);
            int y1 = (int)LOBYTE(pSrcPel[x + 1]);
            int v0 = (int)HIBYTE(pSrcPel[x + 1]);

            pDestPel[x] = ConvertYCrCbToRGB(y0, v0, u0);
            pDestPel[x + 1] = ConvertYCrCbToRGB(y1, v0, u0);
        }

        pSrc += lSrcStride;
        pDest += lDestStride;
    }

}

void TransformImage_NV12(
    BYTE* pDst, 
    LONG dstStride, 
    const BYTE* pSrc, 
    LONG srcStride,
    DWORD dwWidthInPixels,
    DWORD dwHeightInPixels
)
{
    const BYTE* lpBitsY = pSrc;
    const BYTE* lpBitsCb = lpBitsY  + (dwHeightInPixels * srcStride);
    const BYTE* lpBitsCr = lpBitsCb + 1;

    for (UINT y = 0; y < dwHeightInPixels; y += 2) {
        const BYTE* lpLineY1 = lpBitsY;
        const BYTE* lpLineY2 = lpBitsY + srcStride;
        const BYTE* lpLineCr = lpBitsCr;
        const BYTE* lpLineCb = lpBitsCb;

        LPBYTE lpDibLine1 = pDst;
        LPBYTE lpDibLine2 = pDst + dstStride;

        for (UINT x = 0; x < dwWidthInPixels; x += 2) {
            int  y0 = (int)lpLineY1[0];
            int  y1 = (int)lpLineY1[1];
            int  y2 = (int)lpLineY2[0];
            int  y3 = (int)lpLineY2[1];
            int  cb = (int)lpLineCb[0];
            int  cr = (int)lpLineCr[0];

            RGBQUAD r = ConvertYCrCbToRGB(y0, cr, cb);
            lpDibLine1[0] = r.rgbBlue;
            lpDibLine1[1] = r.rgbGreen;
            lpDibLine1[2] = r.rgbRed;
            lpDibLine1[3] = 0; // Alpha

            r = ConvertYCrCbToRGB(y1, cr, cb);
            lpDibLine1[4] = r.rgbBlue;
            lpDibLine1[5] = r.rgbGreen;
            lpDibLine1[6] = r.rgbRed;
            lpDibLine1[7] = 0; // Alpha

            r = ConvertYCrCbToRGB(y2, cr, cb);
            lpDibLine2[0] = r.rgbBlue;
            lpDibLine2[1] = r.rgbGreen;
            lpDibLine2[2] = r.rgbRed;
            lpDibLine2[3] = 0; // Alpha

            r = ConvertYCrCbToRGB(y3, cr, cb);
            lpDibLine2[4] = r.rgbBlue;
            lpDibLine2[5] = r.rgbGreen;
            lpDibLine2[6] = r.rgbRed;
            lpDibLine2[7] = 0; // Alpha

            lpLineY1 += 2;
            lpLineY2 += 2;
            lpLineCr += 2;
            lpLineCb += 2;

            lpDibLine1 += 8;
            lpDibLine2 += 8;
        }

        pDst += (2 * dstStride);
        lpBitsY   += (2 * srcStride);
        lpBitsCr  += srcStride;
        lpBitsCb  += srcStride;
    }
}

HRESULT GetDefaultStride(IMFMediaType *pType, LONG *plStride)
{
    LONG lStride = 0;
    HRESULT hr = pType->GetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32*)&lStride);
    if (SUCCEEDED(hr)) {
        *plStride = lStride;
        return hr;
    }

    GUID subtype = GUID_NULL;
    UINT32 width = 0;
    UINT32 height = 0;
    hr = pType->GetGUID(MF_MT_SUBTYPE, &subtype);
    if (FAILED(hr))
        return hr;

    hr = MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &width, &height);
    if (FAILED(hr))
        return hr;

    hr = MFGetStrideForBitmapInfoHeader(subtype.Data1, width, &lStride);
    if (FAILED(hr))
        return hr;
    
    pType->SetUINT32(MF_MT_DEFAULT_STRIDE, UINT32(lStride));
    *plStride = lStride;

    return hr;
}

VideoBufferLock::VideoBufferLock(IMFMediaBuffer* pBuffer)
{
    m_pBuffer = pBuffer;
    m_pBuffer->AddRef();

    (void)m_pBuffer->QueryInterface(IID_PPV_ARGS(&m_p2DBuffer));
}

VideoBufferLock::~VideoBufferLock()
{
    UnlockBuffer();
    SafeRelease(&m_pBuffer);
    SafeRelease(&m_p2DBuffer);
}

HRESULT VideoBufferLock::LockBuffer(
    LONG  lDefaultStride,
    DWORD dwHeightInPixels,
    BYTE** ppbScanLine0,
    LONG* plStride
)
{
    HRESULT hr = S_OK;
    if (m_p2DBuffer) {
        hr = m_p2DBuffer->Lock2D(ppbScanLine0, plStride);
    }
    else
    {
        BYTE* pData = NULL;
        hr = m_pBuffer->Lock(&pData, NULL, NULL);
        if (SUCCEEDED(hr)) {
            *plStride = lDefaultStride;
            if (lDefaultStride < 0)
                *ppbScanLine0 = pData + abs(lDefaultStride) * (dwHeightInPixels - 1);
            else
                *ppbScanLine0 = pData;
        }
    }

    m_bLocked = (SUCCEEDED(hr));
    return hr;
}

void VideoBufferLock::UnlockBuffer()
{
    if (m_bLocked) {
        if (m_p2DBuffer)
            m_p2DBuffer->Unlock2D();
        else
            m_pBuffer->Unlock();

        m_bLocked = FALSE;
    }
}

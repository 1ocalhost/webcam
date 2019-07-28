#include "previewer.h"

#include <shlwapi.h>
#include <mfapi.h>

#include "util.h"

#define HR_FAIL_RET(x) { \
    if (FAILED(x)) \
        return x; \
}

bool Previewer::Init(LayeredWindow* layered_win)
{
    draw_.Init(layered_win);
    return true;
}

Previewer::~Previewer()
{
    CloseDevice();
}

void Previewer::CloseDevice()
{
    std::unique_lock<std::mutex> lock(mtx_);
    SafeRelease(&reader_);
}

HRESULT Previewer::QueryInterface(REFIID riid, void** ppv)
{
    static const QITAB qit[] = {
        QITABENT(Previewer, IMFSourceReaderCallback),
        { 0 },
    };

    return QISearch(this, qit, riid, ppv);
}

ULONG Previewer::AddRef()
{
    return 0;
}

ULONG Previewer::Release()
{
    return 0;
}

HRESULT Previewer::OnEvent(DWORD, IMFMediaEvent*)
{
    return S_OK;
}

HRESULT Previewer::OnFlush(DWORD)
{
    return S_OK;
}

HRESULT Previewer::RequestNextFrame()
{
    return reader_->ReadSample(
        (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
        0, NULL, NULL, NULL, NULL);
}

HRESULT Previewer::OnReadSample(
    HRESULT status,
    DWORD stream_index,
    DWORD stream_flags,
    LONGLONG timestamp,
    IMFSample* sample)
{
    UNUSED(stream_index);
    UNUSED(stream_flags);
    UNUSED(timestamp);

    HRESULT hr = status;
    IMFMediaBuffer* buffer = NULL;
    std::unique_lock<std::mutex> lock(mtx_);

    if (SUCCEEDED(hr)) {
        if (sample) {
            hr = sample->GetBufferByIndex(0, &buffer);
            if (SUCCEEDED(hr))
                hr = draw_.DrawFrame(buffer);
        }
    }

    // Request the next frame.
    if (SUCCEEDED(hr))
        hr = RequestNextFrame();

    SafeRelease(&buffer);
    return hr;
}

HRESULT Previewer::GetSymbolicLink(IMFActivate* act)
{
    PWSTR value = NULL;
    UINT32 cch = 0;

    HRESULT hr = S_OK;
    hr = act->GetAllocatedString(
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
        &value, &cch);

    if (SUCCEEDED(hr)) {
        symbolic_link_ = value;
        CoTaskMemFree(value);
    }

    return hr;
}

HRESULT Previewer::TryMediaType(IMFMediaType* type)
{
    HRESULT hr = S_OK;
    GUID subtype = {};
    hr = type->GetGUID(MF_MT_SUBTYPE, &subtype);
    HR_FAIL_RET(hr);

    if (draw_.IsFormatSupported(subtype))
        return draw_.SetVideoType(type);

    for (DWORD i = 0; ; i++) {
        hr = draw_.GetFormat(i, &subtype);
        HR_FAIL_RET(hr);

        hr = type->SetGUID(MF_MT_SUBTYPE, subtype);
        HR_FAIL_RET(hr);

        hr = reader_->SetCurrentMediaType(
            (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            NULL, type);

        if (SUCCEEDED(hr))
            return draw_.SetVideoType(type);
    }
}

HRESULT Previewer::CheckSupportedMediaType()
{
    HRESULT hr = S_OK;
    IMFMediaType* type = NULL;

    for (DWORD i = 0;; i++) {
        hr = reader_->GetNativeMediaType(
            (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            i, &type);

        HR_FAIL_RET(hr);

        hr = TryMediaType(type);
        SafeRelease(&type);

        if (SUCCEEDED(hr))
            return S_OK;
    }
}

HRESULT Previewer::SetDevice(IMFActivate* act, SIZE* frame_size)
{
    HRESULT hr = S_OK;
    CloseDevice();
    std::unique_lock<std::mutex> lock(mtx_);

    IMFMediaSource* source = NULL;
    hr = act->ActivateObject(__uuidof(IMFMediaSource), (void**)&source);
    HR_FAIL_RET(hr);
    SCOPE_EXIT([&]() { SafeRelease(&source); });

    hr = GetSymbolicLink(act);
    HR_FAIL_RET(hr);
        
    IMFAttributes* attributes = NULL;
    hr = MFCreateAttributes(&attributes, 2);
    HR_FAIL_RET(hr);
    SCOPE_EXIT([&]() { SafeRelease(&attributes); });

    hr = attributes->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, TRUE);
    HR_FAIL_RET(hr);

    hr = attributes->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, this);
    HR_FAIL_RET(hr);

    hr = MFCreateSourceReaderFromMediaSource(source, attributes, &reader_);
    HR_FAIL_RET(hr);

    hr = CheckSupportedMediaType();
    HR_FAIL_RET(hr);

    *frame_size = draw_.FrameSize();
    hr = RequestNextFrame();

    if (FAILED(hr))
    {
        if (source)
            source->Shutdown();

        CloseDevice();
    }

    return hr;
}

bool Previewer::IsDeviceLost(PDEV_BROADCAST_HDR hdr)
{
    if (!hdr)
        return false;

    if (hdr->dbch_devicetype != DBT_DEVTYP_DEVICEINTERFACE)
        return false;

    std::unique_lock<std::mutex> lock(mtx_);
    PCWSTR name = ((DEV_BROADCAST_DEVICEINTERFACE*)hdr)->dbcc_name;
    return symbolic_link_.size()
        && (_wcsicmp(symbolic_link_.c_str(), name) == 0);
}

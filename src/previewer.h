#pragma once
#include <mfidl.h>
#include <mfreadwrite.h>
#include <dbt.h>  // PDEV_BROADCAST_HDR

#include <mutex>
#include "draw_device.h"

class LayeredWindow;

class Previewer : public IMFSourceReaderCallback
{
public:
    bool Init(LayeredWindow* layered_win);
    virtual ~Previewer();

    // IUnknown methods
    STDMETHODIMP QueryInterface(REFIID iid, void** ppv);
    STDMETHODIMP_(ULONG) AddRef();
    STDMETHODIMP_(ULONG) Release();

    // IMFSourceReaderCallback methods
    STDMETHODIMP OnEvent(DWORD, IMFMediaEvent*);
    STDMETHODIMP OnFlush(DWORD);
    STDMETHODIMP OnReadSample(
        HRESULT status,
        DWORD stream_index,
        DWORD stream_flags,
        LONGLONG timestamp,
        IMFSample* sample);

    HRESULT SetDevice(IMFActivate* act, SIZE* frame_size);
    void CloseDevice();
    bool IsDeviceLost(PDEV_BROADCAST_HDR hdr);

    HRESULT RequestNextFrame();

private:
    HRESULT GetSymbolicLink(IMFActivate* act);
    HRESULT TryMediaType(IMFMediaType* type);
    HRESULT CheckSupportedMediaType();

    std::mutex mtx_;
    DrawDevice draw_;
    IMFSourceReader* reader_ = NULL;
    std::wstring symbolic_link_;
};
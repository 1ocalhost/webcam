#include "window.h"
#include <mfapi.h>
#include <mferror.h>
#include <ks.h> // KSCATEGORY_CAPTURE

#pragma warning(push)
#pragma warning(disable:4458)
#include <gdiplus.h>
#pragma warning(pop)

#include <sstream>
#include "util.h"

__forceinline void LayeredWindowCastPixel(RGBQUAD* p, BYTE a)
{
    if (!a) {
        ZeroMemory(p, sizeof(RGBQUAD));
        return;
    }

    const double ratio = ((double)a / 255);
    p->rgbReserved = a;
    SafeMulti(&p->rgbRed, ratio);
    SafeMulti(&p->rgbGreen, ratio);
    SafeMulti(&p->rgbBlue, ratio);
}

void MemoryDC::Create(HWND hwnd, SIZE size)
{
    hwnd_ = hwnd;
    size_ = size;

    HDC hdc = GetDC(hwnd);
    CreateBitmap(hdc);
    ReleaseDC(hwnd, hdc);

    SetStretchBltMode(mem_dc_, COLORONCOLOR);
}

SIZE MemoryDC::Size()
{
    return size_;
}

RGBQUAD* MemoryDC::Data()
{
    return raw_data_;
}

HWND MemoryDC::Window()
{
    return hwnd_;
}

MemoryDC::operator HDC()
{
    return mem_dc_;
}

const BITMAPINFO* MemoryDC::BmpInfo()
{
    return &bmp_info_;
}

void MemoryDC::StretchTo(MemoryDC* dst_dc, SIZE size)
{
    StretchDIBits(*dst_dc, 0, 0, size.cx, size.cy,
        0, 0, Size().cx, Size().cy, Data(), BmpInfo(), DIB_RGB_COLORS, SRCCOPY);
}

void MemoryDC::Clear()
{
    int byte_num = size_.cx * size_.cy * sizeof(RGBQUAD);
    ZeroMemory(Data(), byte_num);
}

void MemoryDC::UpdateLayered(double opacity)
{
    if (!hwnd_)
        return;

    BYTE alpha = 0xFF;
    if (opacity != 1.0)
        SafeMulti(&alpha, opacity);

    POINT pt_src = { 0, 0 };
    BLENDFUNCTION blend_func = { AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA };
    SIZE size = Size();
    ::UpdateLayeredWindow(hwnd_, NULL, NULL, &size,
        mem_dc_, &pt_src, 0, &blend_func, ULW_ALPHA);
}

void MemoryDC::CreateBitmap(HDC hdc)
{
    bmp_info_.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmp_info_.bmiHeader.biWidth = size_.cx;
    bmp_info_.bmiHeader.biHeight = size_.cy;
    bmp_info_.bmiHeader.biPlanes = 1;
    bmp_info_.bmiHeader.biBitCount = 32;
    bmp_info_.bmiHeader.biCompression = BI_RGB;
    bmp_info_.bmiHeader.biSizeImage = size_.cx * size_.cy * 4;
    bmp_info_.bmiHeader.biXPelsPerMeter = 0;
    bmp_info_.bmiHeader.biClrImportant = 0;
    bmp_info_.bmiHeader.biClrUsed = 0;

    mem_dc_ = CreateCompatibleDC(hdc);
    bitmap_ = CreateDIBSection(mem_dc_, &bmp_info_,
        DIB_RGB_COLORS, (void**)& raw_data_, NULL, 0);

    SelectObject(mem_dc_, bitmap_);
}

void LayeredWindow::Create(HWND hwnd, SIZE size)
{
    content_dc_.Create(hwnd, size);

    SIZE x2_size = { size.cx * 2, size.cy * 2 };
    scale_x1_dc_.Create(hwnd, size);
    scale_x2_dc_.Create(hwnd, x2_size);

    bmp_buf_.reset(new RGBQUAD[size.cx * size.cy]);
    bmp_stride_ = size.cx * sizeof(RGBQUAD);
}

void LayeredWindow::Reset(HWND hwnd, SIZE size)
{
    if (!size.cx || !size.cy)
        return;

    if (size == content_dc_.Size())
        return;

    content_dc_.Release();
    scale_x1_dc_.Release();
    scale_x2_dc_.Release();
    bmp_buf_.release();

    Create(hwnd, size);
}

RGBQUAD* LayeredWindow::BmpBuffer()
{
    return bmp_buf_.get();
}

int LayeredWindow::BmpStride()
{
    return bmp_stride_;
}

void LayeredWindow::OnNewFrame()
{
    SIZE size = content_dc_.Size();
    RGBQUAD* src = BmpBuffer();
    int reverse_offset = size.cx * (size.cy - 1);
    RGBQUAD* dst = content_dc_.Data() + reverse_offset;

    const bool mirror_mode = mirror_mode_;
    for (int y = 0; y < size.cy; ++y) {
        if (mirror_mode)
            ReverseMemcpy<RGBQUAD>(dst, src, size.cx);
        else
            memcpy(dst, src, size.cx * sizeof(RGBQUAD));

        src += size.cx;
        dst -= size.cx;
    }

    Update();
}

void LayeredWindow::OnFrameError(HRESULT hr)
{
    std::wstring msg;
    if (hr == MF_E_HW_MFT_FAILED_START_STREAMING) {
        msg = L"Another app is using the camera already.";
    }
    else {
        std::wstringstream ss;
        ss << "Error: 0x" << std::uppercase << std::hex << hr;
        msg = ss.str();
    }

    using namespace Gdiplus;
    SIZE size = content_dc_.Size();
    content_dc_.Clear();
    Graphics graph((HDC)content_dc_);

    LinearGradientBrush bg_brush(Rect(0, 0, size.cx * 2, size.cy),
        Color(255, 0, 212, 255), Color(255, 0, 25, 29),
        LinearGradientModeBackwardDiagonal);
    graph.FillRectangle(&bg_brush, 0, 0, size.cx, size.cy);

    Font font(&FontFamily(L"Arial"), 12);
    SolidBrush text_brush(Color(200, 255, 255, 255));
    graph.DrawString(msg.c_str(), -1, &font, PointF(10, 10), &text_brush);

    Update();
}

bool LayeredWindow::IsMirrorMode() const
{
    return mirror_mode_;
}

void LayeredWindow::ToggleMirrorMode()
{
    mirror_mode_ = !mirror_mode_;
}

bool LayeredWindow::IsMaskMode() const
{
    return mask_mode_;
}

void LayeredWindow::ToggleMaskMode()
{
    mask_mode_ = !mask_mode_;
}

double LayeredWindow::Scale() const
{
    return scale_;
}

void LayeredWindow::SetScale(double v)
{
    scale_ = v;
}

double LayeredWindow::Opacity() const
{
    return opacity_;
}

void LayeredWindow::SetOpacity(double v)
{
    opacity_ = v;
}

void LayeredWindow::ResetWindowPos()
{
    reset_win_pos_ = true;
}

void LayeredWindow::ResetWindowPos(int win_width)
{
    if (!reset_win_pos_)
        return;

    reset_win_pos_ = false;

    HWND hwnd = content_dc_.Window();
    RECT rect = {};
    GetWindowRect(hwnd, &rect);
    const int min_visable_width = 30;
    if (rect.left + win_width < min_visable_width)
        SetWindowPos(hwnd, NULL, 0, rect.top, NULL, NULL,
            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void LayeredWindow::Update()
{
    SIZE display_size;
    MemoryDC* dc = SelectDisplayDc(&display_size);
    if (!dc)
        return;

    if (mask_mode_)
        BlendMask(dc, display_size);

    dc->UpdateLayered(opacity_);
    ResetWindowPos(display_size.cx);
}

void LayeredWindow::BlendMask(MemoryDC* dc, SIZE display_size)
{
    SIZE size = dc->Size();
    RGBQUAD* dst = dc->Data();

    int reverse_h = size.cy - display_size.cy;
    BYTE* mask = PrepareMask(display_size);
    for (int y = 0; y < size.cy; ++y) {
        if (y >= reverse_h) {
            for (int x = 0; x < display_size.cx; ++x)
                LayeredWindowCastPixel(&dst[x], mask[x]);

            mask += display_size.cx;
        }

        dst += size.cx;
    }
}

MemoryDC* LayeredWindow::SelectDisplayDc(SIZE* display_size)
{
    *display_size = content_dc_.Size();
    if (scale_ == 1.0)
        return &content_dc_;

    MemoryDC* scale_dc = nullptr;
    double scale = scale_;

    if (scale < 1.0) {
        scale_dc = &scale_x1_dc_;
    }
    else if (scale <= 2.0) {
        scale_dc = &scale_x2_dc_;
    }
    else {
        DebugBreak();
        return nullptr;
    }

    if (scale != pre_scale_) {
        scale_dc->Clear();
        pre_scale_ = scale;
    }

    SafeMulti(&display_size->cx, scale);
    SafeMulti(&display_size->cy, scale);
    content_dc_.StretchTo(scale_dc, *display_size);
    return scale_dc;
}

BYTE* LayeredWindow::PrepareMask(SIZE size)
{
    using namespace Gdiplus;

    if (size.cx == mask_size_.cx
        && size.cy == mask_size_.cy
        && mask_data_.get())
        return mask_data_.get();

    mask_size_ = size;
    INT ox = size.cx / 2;
    INT oy = size.cy / 2;
    INT radius = min(ox, oy);
    INT ellipse_size = radius * 2;

    MemoryDC mask_dc;
    mask_dc.Create(content_dc_.Window(), size);
    Graphics graph((HDC)mask_dc);
    SolidBrush brush(Color(255, 0, 0, 0));
    graph.SetSmoothingMode(SmoothingMode::SmoothingModeAntiAlias);
    graph.FillEllipse(&brush, ox - radius, 0, ellipse_size, ellipse_size);

    const int pixel_num = size.cx * size.cy;
    mask_data_.reset(new BYTE[size.cx * size.cy]);
    BYTE* data = mask_data_.get();
    RGBQUAD* dc_data = mask_dc.Data();

    for (int i = 0; i < pixel_num; ++i) {
        *data = dc_data->rgbReserved;
        ++data;
        ++dc_data;
    }

    return mask_data_.get();
}

DeviceSelector::DeviceSelector(MainWindow* win)
{
    win_ = win;
}

bool DeviceSelector::List()
{
    HRESULT hr = S_OK;
    hr = MFCreateAttributes(&attr_, 1);
    if (FAILED(hr)) {
        attr_ = nullptr;
        return false;
    }

    hr = attr_->SetGUID(
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

    if (FAILED(hr))
        return false;

    hr = MFEnumDeviceSources(attr_, &devices_, &dev_num_);
    if (FAILED(hr)) {
        devices_ = nullptr;
        dev_num_ = 0;
        return false;
    }

    return true;
}

bool DeviceSelector::Select(int dev_id, std::function<void(SIZE)> get_size)
{
    IMFActivate* act = devices_[dev_id];
    if (!act)
        return false;

    return win_->SelectDevice(act, get_size);
}

UINT32 DeviceSelector::DevNum() const
{
    return dev_num_;
}

IMFActivate* DeviceSelector::operator[](int n) const
{
    return devices_[n];
}

DeviceSelector::~DeviceSelector()
{
    if (devices_) {
        for (DWORD i = 0; i < dev_num_; i++)
            devices_[i]->Release();

        CoTaskMemFree(devices_);
        devices_ = nullptr;
    }

    if (attr_) {
        attr_->Release();
        attr_ = nullptr;
    }
}

MemoryDC::~MemoryDC()
{
    Release();
}

void MemoryDC::Release()
{
    if (mem_dc_) {
        DeleteDC(mem_dc_);
        mem_dc_ = NULL;
    }

    if (bitmap_) {
        DeleteObject(bitmap_);
        bitmap_ = NULL;
    }
}

MainWindow::~MainWindow()
{
    DestroyWindow();
}

bool MainWindow::Init(std::wstring* msg)
{
    Gdiplus::GdiplusStartupInput gdip_input;
    Gdiplus::GdiplusStartup(&gdip_token_, &gdip_input, NULL);

    HRESULT hr = S_OK;
    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr))
        return false;

    hr = MFStartup(MF_VERSION);
    if (FAILED(hr))
        return false;

    RECT win_rect;
    win_rect.left = 0;
    win_rect.top = 0;
    win_rect.right = 100;
    win_rect.bottom = 100;

    Create(NULL, win_rect, ProgramName(), WS_POPUP, WS_EX_LAYERED | WS_EX_TOPMOST);

    if (!m_hWnd)
        return false;

    DEV_BROADCAST_DEVICEINTERFACE di = { 0 };
    di.dbcc_size = sizeof(di);
    di.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    di.dbcc_classguid = KSCATEGORY_CAPTURE;
    hdev_notify_ = RegisterDeviceNotification(
        m_hWnd,
        &di,
        DEVICE_NOTIFY_WINDOW_HANDLE
    );

    if (!previewer_.Init(&layered_win_))
        return false;

    DeviceSelector dev(this);
    if (!dev.List())
        return false;

    if (!dev.DevNum()) {
        *msg = L"No camera fonud!";
        return false;
    }

    return dev.Select(0, [this](SIZE size) {
        SetCenterIn(size, CurScreenRect());
    });
}

void MainWindow::CleanUp()
{
    if (hdev_notify_)
        UnregisterDeviceNotification(hdev_notify_);

    previewer_.CloseDevice();
    previewer_.Release();

    MFShutdown();
    CoUninitialize();

    Gdiplus::GdiplusShutdown(gdip_token_);
}

LRESULT MainWindow::OnRButtonDown(
    UINT msg, WPARAM wp, LPARAM lp, BOOL& handled) {
    UNUSED(msg);
    UNUSED(wp);
    UNUSED(handled);

    ShowMenu(lp);
    return 0;
}

LRESULT MainWindow::OnNcHitTest(UINT msg, WPARAM wp, LPARAM lp, BOOL& handled) {
    UNUSED(msg);
    UNUSED(wp);
    UNUSED(lp);

    handled = TRUE;
    return HTCAPTION;
}

LRESULT MainWindow::OnDeviceChange(UINT msg, WPARAM wp, LPARAM lp, BOOL& handled) {
    UNUSED(msg);
    UNUSED(wp);
    UNUSED(handled);

    PDEV_BROADCAST_HDR hdr = (PDEV_BROADCAST_HDR)lp;
    if (previewer_.IsDeviceLost(hdr)) {
        previewer_.CloseDevice();
        InfoMsg(L"Lost the capture device.");
    }

    return 0;
}

LRESULT MainWindow::OnClose(UINT msg, WPARAM wp, LPARAM lp, BOOL& handled) {
    UNUSED(msg);
    UNUSED(wp);
    UNUSED(lp);
    UNUSED(handled);

    CleanUp();
    PostMessage(WM_QUIT);
    return 0;
}

PCWSTR MainWindow::ProgramName()
{
    return L"WebcamViewer";
}

void MainWindow::ErrorMsg(PCWSTR msg)
{
    ::MessageBoxW(m_hWnd, msg, ProgramName(), MB_ICONERROR);
}

void MainWindow::InfoMsg(PCWSTR msg)
{
    ::MessageBoxW(m_hWnd, msg, ProgramName(), MB_ICONINFORMATION);
}

int MainWindow::Exec(int show_cmd)
{
    ShowWindow(show_cmd);
    UpdateWindow();

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}

bool MainWindow::SelectDevice(IMFActivate* act, std::function<void(SIZE)> get_size)
{
    dev_uid_ = GetDevPropStr(act,
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK);

    HRESULT hr = previewer_.SetDevice(act,
        [this, get_size](SIZE size) {
        layered_win_.Reset(m_hWnd, size);

        if (get_size)
            get_size(size);
    });

    if (FAILED(hr))
        return false;

    return true;
}

void MainWindow::SetCenterIn(SIZE self_size, const RECT& rect)
{
    LONG x = (rect.right + rect.left - self_size.cx) / 2;
    LONG y = (rect.bottom + rect.top - self_size.cy) / 2;

    if (x < rect.left)
        x = rect.left;

    if (y < rect.top)
        y = rect.top;

    SetWindowPos(NULL, x, y,
        self_size.cx, self_size.cy, SWP_NOZORDER);
}

RECT MainWindow::CurScreenRect()
{
    POINT cursorPos;
    GetCursorPos(&cursorPos);

    HMONITOR monitor = MonitorFromPoint(
        cursorPos, MONITOR_DEFAULTTONEAREST);

    MONITORINFOEX mix;
    mix.cbSize = sizeof(mix);
    if (!GetMonitorInfo(monitor, (LPMONITORINFO)& mix)) {
        mix.rcMonitor = { 0, 0,
            GetSystemMetrics(SM_CXSCREEN),
            GetSystemMetrics(SM_CYSCREEN) };
    }

    return mix.rcMonitor;
}

#include "window.h"
#include <mfapi.h>
#include <mfidl.h>
#include <ks.h> // KSCATEGORY_CAPTURE

#pragma warning(push)
#pragma warning(disable:4458)
#include <gdiplus.h>
#pragma warning(pop)

#include <sstream>
#include "util.h"

struct ChooseDeviceParam {
    IMFActivate** ppDevices;
    UINT32 count;
    UINT32 selection;
};

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

MemoryDC::~MemoryDC()
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

HDC MemoryDC::Dc()
{
    return mem_dc_;
}

const BITMAPINFO* MemoryDC::BmpInfo()
{
    return &bmp_info_;
}

void MemoryDC::StretchTo(MemoryDC* dst_dc, SIZE size)
{
    StretchDIBits(dst_dc->Dc(), 0, 0, size.cx, size.cy,
        0, 0, Size().cx, Size().cy, Data(), BmpInfo(), DIB_RGB_COLORS, SRCCOPY);
}

void MemoryDC::Clear()
{
    int byte_num = size_.cx * size_.cy * sizeof(RGBQUAD);
    ZeroMemory(Data(), byte_num);
}

void MemoryDC::UpdateLayered()
{
    if (!hwnd_)
        return;

    POINT pt_src = { 0, 0 };
    BLENDFUNCTION blend_func = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
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

void LayeredWindow::Reset(HWND hwnd, SIZE size)
{
    content_dc_.Create(hwnd, size);

    SIZE x2_size = { size.cx * 2, size.cy * 2 };
    scale_x1_dc_.Create(hwnd, size);
    scale_x2_dc_.Create(hwnd, x2_size);

    bmp_buf_.reset(new RGBQUAD[size.cx * size.cy]);
    bmp_stride_ = size.cx * sizeof(RGBQUAD);
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

void LayeredWindow::Update()
{
    SIZE display_size;
    MemoryDC* dc = SelectDisplayDc(&display_size);
    if (!dc)
        return;

    if (mask_mode_)
        BlendMask(dc, display_size);

    dc->UpdateLayered();
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
    Graphics graph(mask_dc.Dc());
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

MainWindow::~MainWindow()
{
    DestroyWindow();
}

bool MainWindow::Init()
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

    if (!previewer_.Init(&layered_win))
        return false;

    return ChooseDevice();
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

LRESULT MainWindow::OnRButtonDown(UINT msg, WPARAM wp, LPARAM lp, BOOL& handled) {
    UNUSED(msg);
    UNUSED(wp);
    UNUSED(handled);

    HMENU menu = CreatePopupMenu();
    HMENU menu_scale = CreatePopupMenu();

    int scale_now = (int)(layered_win.Scale() * 100) + MID_SCALE_BASE;
    for (int scale_val : {50, 75, 100, 125, 150, 200}) {
        int scale = scale_val + MID_SCALE_BASE;
        DWORD state = MF_UNCHECKED;

        if (scale == scale_now)
            state = MF_CHECKED | MF_DISABLED;

        std::wstringstream ss;
        ss << scale_val << "%";
        AppendMenu(menu_scale, state, scale, ss.str().c_str());
    }

    AppendMenu(menu, MF_POPUP, (UINT_PTR)menu_scale, L"Scale");
    AppendMenu(menu, MF_SEPARATOR, NULL, NULL);
    AppendMenu(menu,
        layered_win.IsMirrorMode() ? MF_CHECKED : MF_UNCHECKED,
        MID_MIRROR_MODE, L"Mirror Mode");
    AppendMenu(menu,
        layered_win.IsMaskMode() ? MF_CHECKED : MF_UNCHECKED,
        MID_CIRCLE_MODE, L"Circle Mode");
    AppendMenu(menu, MF_SEPARATOR, NULL, NULL);
    AppendMenu(menu, MF_STRING, MID_QUIT, L"Quit");

    int x = GET_X_LPARAM(lp);
    int y = GET_Y_LPARAM(lp);
    UINT_PTR r = ::TrackPopupMenu(menu, TPM_RETURNCMD, x, y, 0, m_hWnd, NULL);
    ::DestroyMenu(menu_scale);
    ::DestroyMenu(menu);

    HandleUserMenu((MenuId)r);
    return 0;
}

void MainWindow::HandleUserMenu(MenuId menu)
{
    if (MID_MIRROR_MODE == menu) {
        layered_win.ToggleMirrorMode();
    }
    else if (MID_CIRCLE_MODE == menu) {
        layered_win.ToggleMaskMode();
    }
    else if (MID_QUIT == menu) {
        ShowWindow(SW_HIDE);
        PostMessage(WM_CLOSE);
    }
    else if (menu > MID_SCALE_BASE && menu < MID_SCALE_END) {
        double scale = (menu - MID_SCALE_BASE) / 100.0;
        layered_win.SetScale(scale);
    }
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

bool MainWindow::ChooseDevice()
{
    HRESULT hr = S_OK;
    IMFAttributes* attributes = NULL;
    hr = MFCreateAttributes(&attributes, 1);
    if (FAILED(hr))
        return false;

    SCOPE_EXIT([=]() {
        attributes->Release();
        });

    // Ask for video capture devices
    hr = attributes->SetGUID(
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

    if (FAILED(hr))
        return false;

    ChooseDeviceParam param = {};
    hr = MFEnumDeviceSources(attributes, &param.ppDevices, &param.count);
    if (FAILED(hr) || param.count == 0)
        return false;

    UINT device_index = 0;
    auto get_size = [this](SIZE size) {
        layered_win.Reset(m_hWnd, size);
        SetCenterIn(size, CurScreenRect());
    };

    hr = previewer_.SetDevice(param.ppDevices[device_index], get_size);
    if (FAILED(hr))
        return false;

    for (DWORD i = 0; i < param.count; i++)
        param.ppDevices[i]->Release();

    CoTaskMemFree(param.ppDevices);
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

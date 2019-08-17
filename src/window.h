#pragma once
#include <atlbase.h>
#include <atlwin.h>
#include <atltypes.h>

#include <memory>
#include <string>
#include "previewer.h"

struct ChooseDeviceParam {
    IMFActivate** ppDevices;
    UINT32 count;
    std::wstring* pre_dev_id;
};

class MemoryDC
{
public:
    ~MemoryDC();
    void Release();
    void Create(HWND hwnd, SIZE size);
    SIZE Size();
    RGBQUAD* Data();
    HWND Window();
    HDC Dc();
    const BITMAPINFO* BmpInfo();
    void StretchTo(MemoryDC* dst_dc, SIZE size);
    void Clear();
    void UpdateLayered();

private:
    void CreateBitmap(HDC hdc);

    HWND hwnd_ = NULL;
    SIZE size_ = {};
    HBITMAP bitmap_ = NULL;
    BITMAPINFO bmp_info_ = {};
    HDC mem_dc_ = NULL;
    RGBQUAD* raw_data_ = NULL;
};

class LayeredWindow
{
public:
    void Reset(HWND hwnd, SIZE size);
    RGBQUAD* BmpBuffer();
    int BmpStride();
    void OnNewFrame();
    bool IsMirrorMode() const;
    void ToggleMirrorMode();
    bool IsMaskMode() const;
    void ToggleMaskMode();
    double Scale() const;
    void SetScale(double v);
    void ResetWindowPos();

private:
    void Create(HWND hwnd, SIZE size);

    void Update();
    void ResetWindowPos(int win_width);
    void BlendMask(MemoryDC* dc, SIZE display_size);
    MemoryDC* SelectDisplayDc(SIZE* display_size);
    BYTE* PrepareMask(SIZE size);

    MemoryDC content_dc_;
    MemoryDC scale_x1_dc_;
    MemoryDC scale_x2_dc_;

    std::unique_ptr<BYTE> mask_data_;
    SIZE mask_size_ = {};

    int bmp_stride_ = 0;
    std::unique_ptr<RGBQUAD> bmp_buf_;
    bool mirror_mode_ = true;
    bool mask_mode_ = false;
    double scale_ = 1.0;
    double pre_scale_ = scale_;
    bool reset_win_pos_ = false;
};

class MainWindow : public CWindowImpl<MainWindow> {
public:
    BEGIN_MSG_MAP(MainWindow)
        MESSAGE_HANDLER(WM_NCRBUTTONDOWN, OnRButtonDown)
        MESSAGE_HANDLER(WM_NCHITTEST, OnNcHitTest)
        MESSAGE_HANDLER(WM_DEVICECHANGE, OnDeviceChange)
        MESSAGE_HANDLER(WM_CLOSE, OnClose)
    END_MSG_MAP()

    ~MainWindow();
    bool Init();
    int Exec(int show_cmd);
    void CleanUp();
    void ErrorMsg(PCWSTR msg);
    void InfoMsg(PCWSTR msg);

private:
    LRESULT OnRButtonDown(UINT msg, WPARAM wp, LPARAM lp, BOOL& handled);
    LRESULT OnNcHitTest(UINT msg, WPARAM wp, LPARAM lp, BOOL& handled);
    LRESULT OnDeviceChange(UINT msg, WPARAM wp, LPARAM lp, BOOL& handled);
    LRESULT OnClose(UINT msg, WPARAM wp, LPARAM lp, BOOL& handled);

    void ShowMenu(LPARAM lp);
    static PCWSTR ProgramName();
    bool ChooseDevice(std::function<int(const ChooseDeviceParam&)> choose);
    void SetCenterIn(SIZE self_size, const RECT& rect);
    RECT CurScreenRect();

    HDEVNOTIFY hdev_notify_ = NULL;
    Previewer previewer_;
    LayeredWindow layered_win_;
    ULONG_PTR gdip_token_ = NULL;
    std::wstring dev_id_;
};

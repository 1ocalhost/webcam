#include <mfidl.h>
#include <functional>
#include <map>
#include <sstream>

#include "util.h"
#include "window.h"

class PopupMenu {
public:
    typedef std::function<void()> Handler;

    PopupMenu(HWND win)
    {
        id_counter_ = this;
        window_ = win;
        menu_ = CreatePopupMenu();
    }

    PopupMenu(PopupMenu* parent)
    {
        id_counter_ = parent;
        menu_ = CreatePopupMenu();
    }

    ~PopupMenu()
    {
        if (menu_) {
            DestroyMenu(menu_);
            menu_ = NULL;
        }
    }

    void Add(const PopupMenu& sub_menu, PCWSTR name)
    {
        HMENU menu = sub_menu.menu_;
        AppendMenu(menu_, MF_POPUP, (UINT_PTR)menu, name);
    }

    void Add(PCWSTR name, Handler handler)
    {
        Add(name, MF_STRING, handler);
    }

    void Add(PCWSTR name, Handler handler, bool enabled)
    {
        UINT flag = (enabled ? MF_CHECKED : MF_UNCHECKED);
        if (enabled && radio_mode_)
            flag |= MF_DISABLED;

        Add(name, flag, handler);
    }

    void AddSeparator()
    {
        AppendMenu(menu_, MF_SEPARATOR, NULL, NULL);
    }

    void SetRadioMode(bool enabled = true)
    {
        radio_mode_ = enabled;
    }

    void ShowByLParam(LPARAM lp)
    {
        int x = GET_X_LPARAM(lp);
        int y = GET_Y_LPARAM(lp);
        Show(x, y);
    }

    void Show()
    {
        POINT pt = {};
        GetCursorPos(&pt);
        Show(pt.x, pt.y);
    }

private:
    void Show(int x, int y)
    {
        UINT_PTR r = TrackPopupMenu(menu_,
            TPM_RETURNCMD, x, y, 0, window_, NULL);

        if (!r)
            return;

        if (handlers_.find(r) != handlers_.end())
            handlers_[r]();
    }

    void Add(PCWSTR name, UINT flag, Handler handler)
    {
        UINT_PTR& id = id_counter_->menu_id;
        ++id;
        AppendMenu(menu_, flag, id, name);
        if (handler)
            id_counter_->handlers_[id] = handler;
    }

    PopupMenu* id_counter_ = nullptr;
    UINT_PTR menu_id = 0;
    HWND window_ = NULL;
    HMENU menu_ = NULL;
    std::map<UINT_PTR, Handler> handlers_;
    bool radio_mode_ = false;
};


void MakeScaleMenu(PopupMenu* menu, LayeredWindow* win)
{
    menu->SetRadioMode();
    int scale_now = (int)(win->Scale() * 100);

    for (int scale : {50, 75, 100, 125, 150, 200}) {
        std::wstringstream ss;
        ss << scale << "%";
        menu->Add(ss.str().c_str(), [win, scale]() {
            win->SetScale(scale / 100.0);
            win->ResetWindowPos();
        }, scale == scale_now);
    }
}

void MakeOpacityMenu(PopupMenu* menu, LayeredWindow* win)
{
    menu->SetRadioMode();
    int opacity_now = (int)(win->Opacity() * 100);

    for (int opacity = 50; opacity <= 100; opacity += 10) {
        std::wstringstream ss;
        ss << opacity << "%";
        menu->Add(ss.str().c_str(), [win, opacity]() {
            win->SetOpacity(opacity / 100.0);
        }, opacity == opacity_now);
    }
}

int ShowSwitchDeviceMenu(HWND win,
    const DeviceSelector& ds, const std::wstring& pre_uid)
{
    int selection = -1;
    PopupMenu menu(win);
    menu.SetRadioMode();

    for (DWORD i = 0; i < ds.DevNum(); ++i) {
        IMFActivate* act = ds[i];
        std::wstring friendly_name = GetDevPropStr(act,
            MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME);

        if (friendly_name.empty())
            friendly_name = L"unknown";

        std::wstring dev_id = GetDevPropStr(act,
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK);

        menu.Add(friendly_name.c_str(), [&, i]() { selection = i; },
            (pre_uid.size() && dev_id == pre_uid));
    }

    menu.Show();
    return selection;
}

void OnSwitchDevice(MainWindow* win, const std::wstring& dev_uid)
{
    DeviceSelector dev(win);
    if (!dev.List() || !dev.DevNum())
        return;

    int select = ShowSwitchDeviceMenu(*win, dev, dev_uid);
    if (select == -1)
        return;

    dev.Select(select, {});
}

void MainWindow::ShowMenu(LPARAM lp)
{
    PopupMenu menu(m_hWnd);
    menu.Add(L"Always On Top", [this]() {
        ToggleWindowTopMost(m_hWnd);
    }, IsWindowTopMost(m_hWnd));
    menu.AddSeparator();

    PopupMenu scale(&menu);
    MakeScaleMenu(&scale, &layered_win_);
    menu.Add(scale, L"Scale");

    PopupMenu opacity(&menu);
    MakeOpacityMenu(&opacity, &layered_win_);
    menu.Add(opacity, L"Opacity");
    menu.AddSeparator();

    menu.Add(L"Mirror Mode", [this]() {
        layered_win_.ToggleMirrorMode();
    }, layered_win_.IsMirrorMode());

    menu.Add(L"Circle Mode", [this]() {
        layered_win_.ToggleMaskMode();
    }, layered_win_.IsMaskMode());
    menu.AddSeparator();

    menu.Add(L"Switch Device", [this]() {
        OnSwitchDevice(this, dev_uid_);
    });

    menu.Add(L"Quit", [this]() {
        ShowWindow(SW_HIDE);
        PostMessage(WM_CLOSE);
    });

    menu.ShowByLParam(lp);
}

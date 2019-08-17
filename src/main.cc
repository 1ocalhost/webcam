#include "window.h"
#include "util.h"

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "gdiplus.lib")

INT WINAPI wWinMain(
    _In_ HINSTANCE instance,
    _In_opt_ HINSTANCE prev_instance,
    _In_ LPWSTR cmd_line,
    _In_ int show_cmd
)
{
    UNUSED(instance);
    UNUSED(prev_instance);
    UNUSED(cmd_line);

    SetProcessDPIAware();

    MainWindow win;
    if (!win.Init()) {
        win.ErrorMsg(L"Something wrong!");
        return -1;
    }

    return win.Exec(show_cmd);
}

#pragma once

#include <windows.h>
#include <string>

#define UNUSED(x) (void)x

template <typename F>
struct ScopeExit {
    ScopeExit(F f) : f(f) {}
    ~ScopeExit() { f(); }
    F f;
};

template <typename F>
ScopeExit<F> MakeScopeExit(F f) {
    return ScopeExit<F>(f);
};

#define CAT_TOKENS_IMPL(a, b) a ## b
#define CAT_TOKENS(a, b) CAT_TOKENS_IMPL(a, b)
#define SCOPE_EXIT(lamda) \
    auto CAT_TOKENS(scope_exit_, __LINE__) = MakeScopeExit(lamda)

template <class T>
void SafeRelease(T** ppT)
{
    if (*ppT) {
        (*ppT)->Release();
        *ppT = NULL;
    }
}

template <class T>
__forceinline void ReverseMemcpy(T* dst, T* src, int num)
{
    T* _src = src;
    T* _dst = dst + num - 1;

    for (int i = 0; i < num; ++i) {
        *_dst = *_src;
        ++_src;
        --_dst;
    }
}

template <class T>
__forceinline void SafeMulti(T* v, double d)
{
    *v = (T)((*v) * d);
}

inline bool operator== (SIZE a, SIZE b)
{
    return (a.cx == b.cx) && (a.cy == b.cy);
}

inline bool IsWindowTopMost(HWND win)
{
    LONG_PTR ex_style = GetWindowLongPtr(win, GWL_EXSTYLE);
    return (ex_style & WS_EX_TOPMOST);
}

inline void ToggleWindowTopMost(HWND win)
{
    bool top_most = IsWindowTopMost(win);
    HWND after = (top_most ? HWND_NOTOPMOST : HWND_TOPMOST);
    SetWindowPos(win, after, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}

inline std::wstring GetDevPropStr(IMFActivate* act, const IID id)
{
    WCHAR* value = NULL;
    HRESULT hr = act->GetAllocatedString(id, &value, NULL);
    if (FAILED(hr))
        return {};

    std::wstring value_cp = value;
    CoTaskMemFree(value);
    return value_cp;
}

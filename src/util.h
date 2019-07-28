#pragma once

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

#ifdef _WIN32
#include <windows.h>
#include "util.h"
void sleep_ms(int ms) { if (ms > 0) Sleep((DWORD)ms); }
long long now_ms(void) { return (long long)GetTickCount(); }
#else
#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include "util.h"
void sleep_ms(int ms) {
    if (ms <= 0) return;
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}
long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000L;
}
#endif

#ifdef _WIN32
#include <windows.h>
#include <stdlib.h>
#include "threads.h"

struct rm_thread { HANDLE h; void *(*fn)(void *); void *arg; };
struct rm_lock   { CRITICAL_SECTION cs; };

static DWORD WINAPI trampoline(LPVOID p) {
    struct rm_thread *t = (struct rm_thread *)p;
    t->fn(t->arg);
    return 0;
}

rm_thread_t *rm_thread_start(void *(*fn)(void *), void *arg) {
    struct rm_thread *t = (struct rm_thread *)malloc(sizeof(*t));
    if (!t) return NULL;
    t->fn = fn;
    t->arg = arg;
    t->h = CreateThread(NULL, 0, trampoline, t, 0, NULL);
    if (!t->h) { free(t); return NULL; }
    return t;
}

void rm_thread_join(rm_thread_t *t) {
    if (!t) return;
    WaitForSingleObject(t->h, INFINITE);
    CloseHandle(t->h);
    free(t);
}

rm_lock_t *rm_lock_new(void) {
    struct rm_lock *l = (struct rm_lock *)malloc(sizeof(*l));
    if (l) InitializeCriticalSection(&l->cs);
    return l;
}
void rm_lock_free(rm_lock_t *l) { if (l) { DeleteCriticalSection(&l->cs); free(l); } }
void rm_lock(rm_lock_t *l)      { EnterCriticalSection(&l->cs); }
void rm_unlock(rm_lock_t *l)    { LeaveCriticalSection(&l->cs); }

#else
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdlib.h>
#include "threads.h"

struct rm_thread { pthread_t t; };
struct rm_lock   { pthread_mutex_t m; };

rm_thread_t *rm_thread_start(void *(*fn)(void *), void *arg) {
    struct rm_thread *t = (struct rm_thread *)malloc(sizeof(*t));
    if (!t) return NULL;
    if (pthread_create(&t->t, NULL, fn, arg) != 0) { free(t); return NULL; }
    return t;
}

void rm_thread_join(rm_thread_t *t) {
    if (!t) return;
    pthread_join(t->t, NULL);
    free(t);
}

rm_lock_t *rm_lock_new(void) {
    struct rm_lock *l = (struct rm_lock *)malloc(sizeof(*l));
    if (l) pthread_mutex_init(&l->m, NULL);
    return l;
}
void rm_lock_free(rm_lock_t *l) { if (l) { pthread_mutex_destroy(&l->m); free(l); } }
void rm_lock(rm_lock_t *l)      { pthread_mutex_lock(&l->m); }
void rm_unlock(rm_lock_t *l)    { pthread_mutex_unlock(&l->m); }
#endif

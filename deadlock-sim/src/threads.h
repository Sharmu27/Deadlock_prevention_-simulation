#ifndef THREADS_H
#define THREADS_H

/* Tiny portable thread/lock layer: Win32 threads on Windows, pthreads elsewhere.
 * Opaque types keep <windows.h> out of the rest of the code. */
typedef struct rm_thread rm_thread_t;
typedef struct rm_lock   rm_lock_t;

rm_thread_t *rm_thread_start(void *(*fn)(void *), void *arg);
void         rm_thread_join(rm_thread_t *t);      /* waits, then frees */

rm_lock_t *rm_lock_new(void);
void       rm_lock_free(rm_lock_t *l);
void       rm_lock(rm_lock_t *l);
void       rm_unlock(rm_lock_t *l);

#endif

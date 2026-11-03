#ifndef RM_H
#define RM_H
#include "models.h"
#include "threads.h"

/* The Resource Manager: a thread-safe layer that real threads call to get and
 * release shared resources. The active strategy decides what happens. */
typedef enum {
    RM_NONE = 0,      /* no safeguard: grant if free, otherwise wait forever   */
    RM_DETECT,        /* allow deadlock, detect it, abort a victim, restart it */
    RM_BANKERS,       /* avoidance: grant only if the state stays safe         */
    RM_ORDERING,      /* prevention: break circular wait with resource order   */
    RM_NOHOLD         /* prevention: break hold-and-wait (all-or-nothing)      */
} RMMode;

#define RM_OK         1
#define RM_VIOLATION -1
#define RM_ABORTED   -2
#define RM_STOPPED   -3

typedef struct {
    System   sys;
    RMMode   mode;
    rm_lock_t *lock;
    const char *state_file;
    int      time_pct;

    int      stop;
    int      detector_run;
    rm_thread_t *det_thread;
    int      abort_flag[MAX_PROCESSES];

    /* metrics */
    int       deadlocks_detected;
    int       aborts;
    int       restarts;
    int       wait_events;
    int       rejections;
    int       finished_jobs;
    long long wait_ms[MAX_PROCESSES];
    long long last_progress;
    long long start_ms;
} RM;

const char *rm_mode_name(RMMode m);

void rm_init(RM *rm, RMMode mode, int time_pct);   /* then fill rm->sys, then rm_begin() */
void rm_begin(RM *rm);                             /* applies policy flags, publishes   */
void rm_destroy(RM *rm);

int  rm_acquire(RM *rm, int pid, const int req[]);          /* blocks; returns RM_* */
void rm_release_held(RM *rm, int pid);                      /* give back, stay alive */
void rm_finish(RM *rm, int pid);                            /* job done              */
void rm_restart_ready(RM *rm, int pid);                     /* after being aborted   */
void rm_note(RM *rm, const char *fmt, ...);                 /* add to event log      */
int  rm_is_stopped(RM *rm);
void rm_request_stop(RM *rm);
void rm_publish_locked(RM *rm);                             /* caller holds lock     */

void rm_start_detector(RM *rm);
void rm_stop_detector(RM *rm);

#endif

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include "rm.h"
#include "detection.h"
#include "bankers.h"
#include "prevention.h"
#include "export.h"
#include "util.h"

#define POLL_MS 15

const char *rm_mode_name(RMMode m) {
    switch (m) {
        case RM_NONE:     return "No safeguard";
        case RM_DETECT:   return "Detection + Recovery";
        case RM_BANKERS:  return "Avoidance (Banker's)";
        case RM_ORDERING: return "Prevention: resource ordering";
        case RM_NOHOLD:   return "Prevention: no hold-and-wait";
    }
    return "?";
}

void rm_init(RM *rm, RMMode mode, int time_pct) {
    memset(rm, 0, sizeof(*rm));
    rm->mode = mode;
    rm->time_pct = time_pct;
    rm->lock = rm_lock_new();
    rm->state_file = "system_state.json";
    rm->start_ms = rm->last_progress = now_ms();
}

void rm_destroy(RM *rm) {
    rm_lock_free(rm->lock);
    rm->lock = NULL;
}

void rm_publish_locked(RM *rm) {
    export_state_json(&rm->sys, rm->state_file, 1);
}

void rm_begin(RM *rm) {
    rm->sys.policy_resource_ordering = (rm->mode == RM_ORDERING);
    rm->sys.policy_no_hold_and_wait  = (rm->mode == RM_NOHOLD);
    rm->start_ms = rm->last_progress = now_ms();
    rm_lock(rm->lock);
    rm_publish_locked(rm);
    rm_unlock(rm->lock);
}

static void describe_req(const System *s, const int *req, char *buf, size_t len) {
    size_t used = 0;
    buf[0] = '\0';
    for (int j = 0; j < s->num_resources; j++) {
        if (req[j] <= 0 || used + 2 >= len) continue;
        int w = snprintf(buf + used, len - used, "%s%s x%d", used ? ", " : "",
                         s->resource_names[j], req[j]);
        if (w < 0) break;
        used += (size_t)w;
        if (used >= len) { used = len - 1; break; }
    }
}

static void clear_pending(System *s, int pid) {
    for (int j = 0; j < s->num_resources; j++) s->request[pid][j] = 0;
}

/* give everything back; caller holds the lock */
static void release_all_locked(RM *rm, int pid) {
    System *s = &rm->sys;
    for (int j = 0; j < s->num_resources; j++) {
        s->available[j] += s->allocation[pid][j];
        s->allocation[pid][j] = 0;
        s->request[pid][j] = 0;
        s->need[pid][j] = s->max_need[pid][j];
    }
}

void rm_note(RM *rm, const char *fmt, ...) {
    char buf[EVENT_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    rm_lock(rm->lock);
    log_event(&rm->sys, "%s", buf);
    rm_publish_locked(rm);
    rm_unlock(rm->lock);
}

int rm_is_stopped(RM *rm) {
    rm_lock(rm->lock);
    int v = rm->stop;
    rm_unlock(rm->lock);
    return v;
}

void rm_request_stop(RM *rm) {
    rm_lock(rm->lock);
    rm->stop = 1;
    rm_unlock(rm->lock);
}

int rm_acquire(RM *rm, int pid, const int req_in[]) {
    System *s = &rm->sys;
    int req[MAX_RESOURCES];
    char what[96], reason[128];
    int result = RM_OK, waiting = 0;
    long long wait_start = 0;

    for (int j = 0; j < s->num_resources; j++) req[j] = req_in[j];
    describe_req(s, req, what, sizeof(what));

    rm_lock(rm->lock);
    for (;;) {
        if (rm->stop)             { result = RM_STOPPED; break; }
        if (rm->abort_flag[pid])  { result = RM_ABORTED; break; }

        /* prevention rules are checked before anything else */
        if (rm->mode == RM_ORDERING) {
            int bad = 0;
            for (int j = 0; j < s->num_resources; j++)
                if (req[j] > 0 && !check_circular_wait(s, pid, j)) bad = 1;
            if (bad) {
                rm->rejections++;
                log_event(s, "%s: asking for %s breaks the lock order - rejected",
                          s->process_names[pid], what);
                clear_pending(s, pid);
                rm_publish_locked(rm);
                result = RM_VIOLATION;
                break;
            }
        }
        if (rm->mode == RM_NOHOLD && !check_hold_and_wait(s, pid)) {
            rm->rejections++;
            result = RM_VIOLATION;
            break;
        }

        int granted = 0;
        reason[0] = '\0';
        if (rm->mode == RM_BANKERS) {
            int code = request_resources(s, pid, req, reason, sizeof(reason));
            if (code == REQ_GRANTED) granted = 1;
            else if (code == REQ_REJECTED) { clear_pending(s, pid); result = RM_VIOLATION; break; }
        } else {
            int ok = 1;
            for (int j = 0; j < s->num_resources; j++)
                if (req[j] > s->available[j]) ok = 0;
            if (ok) {
                for (int j = 0; j < s->num_resources; j++) {
                    s->available[j]       -= req[j];
                    s->allocation[pid][j] += req[j];
                    s->need[pid][j]       -= req[j];
                }
                granted = 1;
            } else {
                for (int j = 0; j < s->num_resources; j++) s->request[pid][j] = req[j];
            }
        }

        if (granted) {
            clear_pending(s, pid);
            log_event(s, "%s acquired %s", s->process_names[pid], what);
            rm->last_progress = now_ms();
            rm_publish_locked(rm);
            result = RM_OK;
            break;
        }

        if (!waiting) {
            waiting = 1;
            wait_start = now_ms();
            rm->wait_events++;
            log_event(s, "%s waits for %s%s%s", s->process_names[pid], what,
                      reason[0] ? " - " : "", reason);
            rm_publish_locked(rm);
        }
        rm_unlock(rm->lock);
        sleep_ms(POLL_MS);
        rm_lock(rm->lock);
    }
    if (waiting) rm->wait_ms[pid] += now_ms() - wait_start;
    rm_unlock(rm->lock);
    return result;
}

void rm_release_held(RM *rm, int pid) {
    rm_lock(rm->lock);
    release_all_locked(rm, pid);
    log_event(&rm->sys, "%s released everything to retry in order", rm->sys.process_names[pid]);
    rm->last_progress = now_ms();
    rm_publish_locked(rm);
    rm_unlock(rm->lock);
}

void rm_finish(RM *rm, int pid) {
    System *s;
    rm_lock(rm->lock);
    s = &rm->sys;
    release_all_locked(rm, pid);
    for (int j = 0; j < s->num_resources; j++) { s->max_need[pid][j] = 0; s->need[pid][j] = 0; }
    s->pstate[pid] = PROC_FINISHED;
    rm->finished_jobs++;
    log_event(s, "%s finished, released everything", s->process_names[pid]);
    rm->last_progress = now_ms();
    rm_publish_locked(rm);
    rm_unlock(rm->lock);
}

void rm_restart_ready(RM *rm, int pid) {
    rm_lock(rm->lock);
    rm->abort_flag[pid] = 0;
    rm->sys.pstate[pid] = PROC_ACTIVE;
    rm->restarts++;
    log_event(&rm->sys, "%s restarts from the beginning", rm->sys.process_names[pid]);
    rm_publish_locked(rm);
    rm_unlock(rm->lock);
}

/* ---------------- deadlock detector thread (Detection + Recovery) ---------------- */

static void abort_victim_locked(RM *rm, int pid) {
    System *s = &rm->sys;
    release_all_locked(rm, pid);
    s->pstate[pid] = PROC_KILLED;
    rm->abort_flag[pid] = 1;
    rm->aborts++;
    log_event(s, "RECOVERY: aborted %s (victim), its resources released", s->process_names[pid]);
    rm->last_progress = now_ms();
}

static int total_alloc(const System *s, int pid) {
    int t = 0;
    for (int j = 0; j < s->num_resources; j++) t += s->allocation[pid][j];
    return t;
}

static void *detector_main(void *arg) {
    RM *rm = (RM *)arg;
    int interval = 300 * rm->time_pct / 100;
    if (interval < 30) interval = 30;

    for (;;) {
        sleep_ms(interval);
        rm_lock(rm->lock);
        if (!rm->detector_run || rm->stop) { rm_unlock(rm->lock); break; }

        System *s = &rm->sys;
        int dl[MAX_PROCESSES], cnt = 0;
        if (detect_deadlock(s, dl, &cnt)) {
            rm->deadlocks_detected++;
            log_event(s, "DETECTOR: deadlock found, %d job(s) stuck", cnt);

            /* prefer a job that is part of the actual cycle, cheapest to abort */
            int wfg[MAX_PROCESSES][MAX_PROCESSES];
            build_wait_for_graph(s, wfg);
            int cyc[MAX_PROCESSES + 1], clen = 0;
            detect_cycle_wfg(wfg, s->num_processes, cyc, &clen);

            int best = -1, best_cost = INT_MAX;
            const int *cand = clen > 0 ? cyc : dl;
            int ncand = clen > 0 ? clen : cnt;
            for (int k = 0; k < ncand; k++) {
                int p = cand[k];
                int cost = total_alloc(s, p);
                if (s->pstate[p] == PROC_ACTIVE && cost > 0 && cost < best_cost) {
                    best = p;
                    best_cost = cost;
                }
            }
            if (best >= 0) abort_victim_locked(rm, best);
            rm_publish_locked(rm);
        }
        rm_unlock(rm->lock);
    }
    return NULL;
}

void rm_start_detector(RM *rm) {
    rm->detector_run = 1;
    rm->det_thread = rm_thread_start(detector_main, rm);
}

void rm_stop_detector(RM *rm) {
    rm_lock(rm->lock);
    rm->detector_run = 0;
    rm_unlock(rm->lock);
    if (rm->det_thread) { rm_thread_join(rm->det_thread); rm->det_thread = NULL; }
}

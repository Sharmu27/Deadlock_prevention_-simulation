/*
 * WORKLOAD LAB  -  fully automatic, no menus.
 *
 * Real OS threads ("jobs") compete for shared devices (printer, scanner, disk,
 * network) through the Resource Manager. The same workload is run under five
 * strategies so their behaviour can be compared:
 *
 *   1. No safeguard            -> deadlock freezes the system
 *   2. Detection + Recovery    -> detect the deadlock, abort a victim, restart it
 *   3. Avoidance (Banker's)    -> never enter an unsafe state
 *   4. Prevention: ordering    -> break circular wait
 *   5. Prevention: no hold&wait-> break hold-and-wait
 *
 * Phase 1 : the classic scenario, shown live on the dashboard.
 * Phase 2 : an automatic stress test on randomly generated workloads.
 * Phase 3 : a verdict computed from the measured results.
 *
 * Usage:   workload [fast] [quick] [N]
 *            fast  - run everything faster
 *            quick - skip the stress test
 *            N     - number of random workloads in the stress test (default 8)
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "models.h"
#include "rm.h"
#include "util.h"

#define MAX_STEPS   4
#define THINK_MS    500    /* pause between a job's acquisitions            */
#define WORK_MS     900    /* time a job spends using everything it holds   */
#define BACKOFF_MS  600    /* restart delay after being aborted (+ pid*250) */
#define NUM_MODES   5

enum { OUT_DONE, OUT_ABORTED, OUT_STOPPED };

typedef struct {
    char name[NAME_LEN];
    int  nsteps;
    int  res[MAX_STEPS];
    int  units[MAX_STEPS];
} JobPlan;

typedef struct {
    int  nres;
    char rnames[MAX_RESOURCES][NAME_LEN];
    int  totals[MAX_RESOURCES];
    int  njobs;
    JobPlan jobs[MAX_PROCESSES];
} Workload;

typedef struct {
    int       completed;
    int       hung;
    long long time_ms;
    int       deadlocks, aborts, waits, rejections;
    long long wait_ms;
} RunResult;

typedef struct {
    RM     *rm;
    int     pid;
    JobPlan plan;
} Job;

static const RMMode MODES[NUM_MODES] = { RM_NONE, RM_DETECT, RM_BANKERS, RM_ORDERING, RM_NOHOLD };

/* ------------------------------------------------------------------ workloads */

static void build_classic(Workload *w) {
    memset(w, 0, sizeof(*w));
    const char *rn[4] = { "Printer", "Scanner", "Disk", "Network" };
    int tot[4] = { 1, 1, 1, 2 };
    w->nres = 4;
    for (int j = 0; j < 4; j++) { snprintf(w->rnames[j], NAME_LEN, "%s", rn[j]); w->totals[j] = tot[j]; }

    /* A, B, C form a classic circular wait. D and E are innocent bystanders
     * who get blocked behind them (as in a real system). */
    JobPlan p[5] = {
        { "Job-A", 2, { 0, 1 }, { 1, 1 } },   /* Printer -> Scanner */
        { "Job-B", 2, { 1, 2 }, { 1, 1 } },   /* Scanner -> Disk    */
        { "Job-C", 2, { 2, 0 }, { 1, 1 } },   /* Disk    -> Printer */
        { "Job-D", 2, { 3, 0 }, { 1, 1 } },   /* Network -> Printer */
        { "Job-E", 1, { 3 },    { 2 } },      /* Network x2         */
    };
    w->njobs = 5;
    for (int i = 0; i < 5; i++) w->jobs[i] = p[i];
}

static unsigned rng_next(unsigned *s) {
    *s = *s * 1103515245u + 12345u;
    return (*s >> 16) & 0x7fff;
}

static void build_random(Workload *w, unsigned seed) {
    unsigned s = seed * 2654435761u + 17u;
    const char *pool[4] = { "Printer", "Scanner", "Disk", "Network" };
    memset(w, 0, sizeof(*w));
    w->nres = 3 + (int)(rng_next(&s) % 2);
    for (int j = 0; j < w->nres; j++) {
        snprintf(w->rnames[j], NAME_LEN, "%s", pool[j]);
        w->totals[j] = 1 + (int)(rng_next(&s) % 2);
    }
    w->njobs = 4 + (int)(rng_next(&s) % 2);
    for (int i = 0; i < w->njobs; i++) {
        JobPlan *p = &w->jobs[i];
        snprintf(p->name, NAME_LEN, "Job-%c", 'A' + i);
        p->nsteps = 2 + (int)(rng_next(&s) % 2);
        int order[MAX_RESOURCES];
        for (int j = 0; j < w->nres; j++) order[j] = j;
        for (int j = w->nres - 1; j > 0; j--) {          /* shuffle */
            int k = (int)(rng_next(&s) % (unsigned)(j + 1));
            int t = order[j]; order[j] = order[k]; order[k] = t;
        }
        for (int k = 0; k < p->nsteps; k++) { p->res[k] = order[k]; p->units[k] = 1; }
    }
}

/* ------------------------------------------------------------------ job threads */

static int sc(const RM *rm, int ms) {
    int v = ms * rm->time_pct / 100;
    return v < 5 ? 5 : v;
}

static void job_sleep(RM *rm, int ms) {      /* sleeps in slices so a stop is quick */
    while (ms > 0 && !rm_is_stopped(rm)) {
        int slice = ms > 25 ? 25 : ms;
        sleep_ms(slice);
        ms -= slice;
    }
}

static void sort_plan(JobPlan *p) {          /* lock-ordering: ascending resource id */
    for (int a = 0; a < p->nsteps; a++)
        for (int b = a + 1; b < p->nsteps; b++)
            if (p->res[b] < p->res[a]) {
                int tr = p->res[a], tu = p->units[a];
                p->res[a] = p->res[b];     p->units[a] = p->units[b];
                p->res[b] = tr;            p->units[b] = tu;
            }
}

static int attempt(Job *job) {
    RM *rm = job->rm;
    int pid = job->pid;
    JobPlan *p = &job->plan;
    int req[MAX_RESOURCES];

    if (rm->mode == RM_NOHOLD) {                 /* all-or-nothing */
        memset(req, 0, sizeof(req));
        for (int k = 0; k < p->nsteps; k++) req[p->res[k]] += p->units[k];
        int r = rm_acquire(rm, pid, req);
        if (r == RM_ABORTED) return OUT_ABORTED;
        if (r != RM_OK) return OUT_STOPPED;
    } else {
        for (int k = 0; k < p->nsteps; k++) {
            memset(req, 0, sizeof(req));
            req[p->res[k]] = p->units[k];
            int r = rm_acquire(rm, pid, req);
            if (r == RM_OK) {
                if (k < p->nsteps - 1) job_sleep(rm, sc(rm, THINK_MS));
                continue;
            }
            if (r == RM_ABORTED) return OUT_ABORTED;
            if (r == RM_VIOLATION && rm->mode == RM_ORDERING) {
                rm_release_held(rm, pid);
                sort_plan(p);
                rm_note(rm, "%s re-plans: will acquire in resource order", rm->sys.process_names[pid]);
                k = -1;
                continue;
            }
            return OUT_STOPPED;
        }
    }
    job_sleep(rm, sc(rm, WORK_MS));
    if (rm_is_stopped(rm)) return OUT_STOPPED;
    rm_finish(rm, pid);
    return OUT_DONE;
}

static void *job_main(void *arg) {
    Job *job = (Job *)arg;
    for (;;) {
        if (attempt(job) != OUT_ABORTED) break;
        job_sleep(job->rm, sc(job->rm, BACKOFF_MS + job->pid * 250));
        if (rm_is_stopped(job->rm)) break;
        rm_restart_ready(job->rm, job->pid);
    }
    return NULL;
}

/* ------------------------------------------------------------------ one run */

static void print_new_events(RM *rm, int *printed) {      /* caller holds the lock */
    System *s = &rm->sys;
    if (s->event_count - *printed > MAX_EVENTS) *printed = s->event_count - MAX_EVENTS;
    while (*printed < s->event_count) {
        printf("      | %s\n", s->events[*printed % MAX_EVENTS]);
        (*printed)++;
    }
}

static void run_workload(const Workload *w, RMMode mode, int pct, int verbose,
                         const char *tag, RunResult *out) {
    RM rm;
    Job jobs[MAX_PROCESSES];
    rm_thread_t *th[MAX_PROCESSES];

    rm_init(&rm, mode, pct);
    System *s = &rm.sys;
    init_system(s, w->njobs, w->nres);
    for (int j = 0; j < w->nres; j++) {
        snprintf(s->resource_names[j], NAME_LEN, "%s", w->rnames[j]);
        s->available[j] = s->total_instances[j] = w->totals[j];
    }
    for (int i = 0; i < w->njobs; i++) {
        snprintf(s->process_names[i], NAME_LEN, "%s", w->jobs[i].name);
        for (int k = 0; k < w->jobs[i].nsteps; k++)
            s->max_need[i][w->jobs[i].res[k]] += w->jobs[i].units[k];
    }
    compute_need_matrix(s);
    snprintf(s->label, sizeof(s->label), "%s  -  %s", tag, rm_mode_name(mode));
    log_event(s, "Started: %s", rm_mode_name(mode));
    rm_begin(&rm);
    if (mode == RM_DETECT) rm_start_detector(&rm);

    for (int i = 0; i < w->njobs; i++) {
        jobs[i].rm = &rm;
        jobs[i].pid = i;
        jobs[i].plan = w->jobs[i];
        th[i] = rm_thread_start(job_main, &jobs[i]);
    }

    /* watchdog: finished, or no progress for a long time = hung */
    int hang_ms = 4000 * pct / 100;  if (hang_ms < 600) hang_ms = 600;
    long long timeout_ms = 90000LL * pct / 100;  if (timeout_ms < 10000) timeout_ms = 10000;
    int printed = 0, hung = 0;
    for (;;) {
        sleep_ms(40);
        rm_lock(rm.lock);
        if (verbose) print_new_events(&rm, &printed);
        int done = (rm.finished_jobs == w->njobs);
        long long idle = now_ms() - rm.last_progress;
        long long total = now_ms() - rm.start_ms;
        rm_unlock(rm.lock);
        if (done) break;
        if (idle > hang_ms || total > timeout_ms) { hung = 1; break; }
    }
    if (hung) rm_request_stop(&rm);
    for (int i = 0; i < w->njobs; i++) rm_thread_join(th[i]);
    rm_stop_detector(&rm);

    rm_lock(rm.lock);
    out->hung = hung;
    out->completed = !hung;
    out->time_ms = now_ms() - rm.start_ms - (hung ? 0 : 0);
    if (hung) {
        log_event(s, "SYSTEM HUNG: no progress - deadlock and nothing to resolve it");
        snprintf(s->label, sizeof(s->label), "%s  -  %s  -  HUNG", tag, rm_mode_name(mode));
    }
    out->deadlocks = rm.deadlocks_detected;
    out->aborts = rm.aborts;
    out->waits = rm.wait_events;
    out->rejections = rm.rejections;
    out->wait_ms = 0;
    for (int i = 0; i < w->njobs; i++) out->wait_ms += rm.wait_ms[i];
    rm_publish_locked(&rm);
    if (verbose) print_new_events(&rm, &printed);
    rm_unlock(rm.lock);
    rm_destroy(&rm);
}

/* ------------------------------------------------------------------ reporting */

static void print_result_row(const char *name, const RunResult *r) {
    char t[16];
    if (r->hung) snprintf(t, sizeof(t), "   -  "); else snprintf(t, sizeof(t), "%5.1fs", r->time_ms / 1000.0);
    printf("  %-31s %-10s %s  %9d  %6d  %5d  %7.1fs\n", name,
           r->hung ? "HUNG" : "completed", t, r->deadlocks, r->aborts, r->waits, r->wait_ms / 1000.0);
}

static void print_header(void) {
    printf("  %-31s %-10s %-7s %9s  %6s  %5s  %8s\n", "Strategy", "Outcome", "Time",
           "Deadlocks", "Aborts", "Waits", "TotalWait");
    printf("  ---------------------------------------------------------------------------------\n");
}

typedef struct {
    int runs, completed, hung, aborts, deadlocks, waits, rejections;
    long long time_ms;
} Agg;

/* ------------------------------------------------------------------ main */

int main(int argc, char **argv) {
    int pct = 100, quick = 0, nstress = 8;
    for (int a = 1; a < argc; a++) {
        if (!strcmp(argv[a], "fast")) pct = 35;
        else if (!strcmp(argv[a], "quick")) quick = 1;
        else if (atoi(argv[a]) > 0) nstress = atoi(argv[a]);
    }

    printf("\n==============================================================================\n");
    printf("  WORKLOAD LAB   -   real threads, one shared Resource Manager, no menus\n");
    printf("  Tip: run  python visualizer\\live_view.py  to watch every step live.\n");
    printf("==============================================================================\n");

    /* ---------------- Phase 1: the classic scenario under every strategy ---------------- */
    Workload classic;
    build_classic(&classic);
    printf("\nPHASE 1 - Classic scenario\n");
    printf("  5 jobs share: Printer x1, Scanner x1, Disk x1, Network x2\n");
    printf("  Job-A: Printer -> Scanner     Job-B: Scanner -> Disk     Job-C: Disk -> Printer\n");
    printf("  Job-D: Network -> Printer     Job-E: Network x2\n");
    printf("  A, B and C each grab one device, then wait for the next one: a circular wait.\n");

    RunResult p1[NUM_MODES];
    for (int m = 0; m < NUM_MODES; m++) {
        char tag[48];
        snprintf(tag, sizeof(tag), "Phase 1 (%d/%d)", m + 1, NUM_MODES);
        printf("\n  [%d/%d] %s\n", m + 1, NUM_MODES, rm_mode_name(MODES[m]));
        run_workload(&classic, MODES[m], pct, 1, tag, &p1[m]);
        if (p1[m].hung) printf("      => FROZEN: nothing can ever finish.\n");
        else printf("      => all 5 jobs finished in %.1f s\n", p1[m].time_ms / 1000.0);
        sleep_ms(700);
    }

    printf("\n  RESULTS - classic scenario\n");
    print_header();
    for (int m = 0; m < NUM_MODES; m++) print_result_row(rm_mode_name(MODES[m]), &p1[m]);
    printf("  (Deadlocks = found by the system itself; only Detection + Recovery runs a detector.)\n");

    FILE *csv = fopen("workload_results.csv", "w");
    if (csv) {
        fprintf(csv, "phase,strategy,runs,completed,hung,avg_time_s,deadlocks,aborts,waits,rejections\n");
        for (int m = 0; m < NUM_MODES; m++)
            fprintf(csv, "classic,\"%s\",1,%d,%d,%.2f,%d,%d,%d,%d\n", rm_mode_name(MODES[m]),
                    p1[m].completed, p1[m].hung, p1[m].hung ? 0.0 : p1[m].time_ms / 1000.0,
                    p1[m].deadlocks, p1[m].aborts, p1[m].waits, p1[m].rejections);
    }

    if (quick) {
        if (csv) fclose(csv);
        printf("\n(quick mode: stress test skipped)  Results saved to workload_results.csv\n");
        return 0;
    }

    /* ---------------- Phase 2: stress test on random workloads ---------------- */
    printf("\nPHASE 2 - Stress test: %d random workloads x %d strategies (automatic)\n", nstress, NUM_MODES);
    printf("  Each workload has a random number of jobs, devices and acquisition orders.\n\n");
    Agg agg[NUM_MODES];
    memset(agg, 0, sizeof(agg));
    int stress_pct = 10;
    int prone = 0;                 /* workloads that really deadlocked without safeguards */

    for (int n = 0; n < nstress; n++) {
        Workload w;
        build_random(&w, (unsigned)(n + 1));
        char tag[48];
        snprintf(tag, sizeof(tag), "Phase 2 stress %d/%d", n + 1, nstress);
        printf("  workload %d/%d (%d jobs, %d devices): ", n + 1, nstress, w.njobs, w.nres);
        fflush(stdout);
        for (int m = 0; m < NUM_MODES; m++) {
            RunResult r;
            run_workload(&w, MODES[m], stress_pct, 0, tag, &r);
            agg[m].runs++;
            agg[m].completed += r.completed;
            agg[m].hung += r.hung;
            agg[m].aborts += r.aborts;
            agg[m].deadlocks += r.deadlocks;
            agg[m].waits += r.waits;
            agg[m].rejections += r.rejections;
            if (r.completed) agg[m].time_ms += r.time_ms;
            if (m == 0 && r.hung) prone++;
            if (r.hung) printf("X");
            else if (r.aborts) printf("ok(%d)", r.aborts);
            else printf("ok");
            printf("%s", m < NUM_MODES - 1 ? " " : "");
            fflush(stdout);
        }
        printf("\n");
    }
    printf("\n  (columns: no-safeguard, detection, banker's, ordering, no-hold-and-wait;  X = hung, ok(n) = recovered with n abort(s))\n");

    printf("\n  RESULTS - stress test (%d workloads)\n", nstress);
    printf("  %-31s %-9s %-7s %-7s %-8s %-9s\n", "Strategy", "Completed", "Hung", "Aborts", "AvgTime", "Rejected");
    printf("  ---------------------------------------------------------------------------\n");
    for (int m = 0; m < NUM_MODES; m++) {
        double avg = agg[m].completed ? agg[m].time_ms / 1000.0 / agg[m].completed : 0.0;
        printf("  %-31s %4d/%-4d %-7d %-7d %6.2fs  %-9d\n", rm_mode_name(MODES[m]),
               agg[m].completed, agg[m].runs, agg[m].hung, agg[m].aborts, avg, agg[m].rejections);
        if (csv)
            fprintf(csv, "stress,\"%s\",%d,%d,%d,%.2f,%d,%d,%d,%d\n", rm_mode_name(MODES[m]),
                    agg[m].runs, agg[m].completed, agg[m].hung, avg, agg[m].deadlocks,
                    agg[m].aborts, agg[m].waits, agg[m].rejections);
    }
    if (csv) fclose(csv);

    /* ---------------- Phase 3: verdict from the measured data ---------------- */
    printf("\nPHASE 3 - Verdict (computed from the results above)\n");
    printf("  * Without any safeguard, %d of %d random workloads froze permanently.\n", prone, nstress);
    int best = -1;
    double best_avg = 1e18;
    for (int m = 1; m < NUM_MODES; m++) {
        int safe = (agg[m].hung == 0 && agg[m].completed == agg[m].runs);
        printf("  * %-31s %s", rm_mode_name(MODES[m]), safe ? "never froze" : "FROZE sometimes");
        if (agg[m].aborts) printf(", but wasted work: %d job(s) aborted and restarted", agg[m].aborts);
        else if (safe) printf(", no aborted jobs");
        printf(".\n");
        if (safe && agg[m].aborts == 0) {
            double avg = agg[m].time_ms / 1000.0 / agg[m].completed;
            if (avg < best_avg) { best_avg = avg; best = m; }
        }
    }
    if (best >= 0)
        printf("  => Best choice here: %s (safe, nothing aborted, fastest at %.2f s average).\n",
               rm_mode_name(MODES[best]), best_avg);
    printf("  => Detection + Recovery is the fallback when jobs cannot declare needs in advance.\n");
    printf("\nResults saved to workload_results.csv\n\n");
    return 0;
}

#include <stdio.h>
#include <string.h>
#include "models.h"
#include "detection.h"
#include "bankers.h"
#include "prevention.h"
#include "recovery.h"
#include "visualize.h"
#include "export.h"
#include "util.h"

#define STATE_FILE "system_state.json"

/* ---------- small helpers ---------- */

static void flush_line(void) {
    int c;
    while ((c = getchar()) != '\n' && c != EOF) {}
}

/* returns 1 ok, 0 bad input, -1 end of input */
static int read_int(const char *prompt, int *out) {
    if (prompt) { printf("%s", prompt); fflush(stdout); }
    int r = scanf("%d", out);
    if (r == EOF) return -1;
    if (r != 1) { flush_line(); return 0; }
    return 1;
}

static void fmt_vec(const int *v, int n, char *buf, size_t len) {
    size_t used = 0;
    buf[0] = '\0';
    for (int j = 0; j < n && used + 8 < len; j++)
        used += (size_t)snprintf(buf + used, len - used, "%s%d", j ? " " : "", v[j]);
}

/* Refresh the live dashboard file after every action */
static void sync_view(System *sys) {
    export_state_json(sys, STATE_FILE, 1);
}

/* ---------- built-in scenarios ---------- */

static void load_scenario_safe(System *sys) {
    init_system(sys, 5, 3);
    int alloc[5][3] = {{0,1,0},{2,0,0},{3,0,2},{2,1,1},{0,0,2}};
    int maxn[5][3]  = {{7,5,3},{3,2,2},{9,0,2},{2,2,2},{4,3,3}};
    int avail[3] = {3,3,2};

    for (int i = 0; i < 5; i++)
        for (int j = 0; j < 3; j++) {
            sys->allocation[i][j] = alloc[i][j];
            sys->max_need[i][j] = maxn[i][j];
        }
    for (int j = 0; j < 3; j++) sys->available[j] = avail[j];
    compute_need_matrix(sys);
    log_event(sys, "Loaded scenario: Safe State (5 processes, 3 resource types)");
}

static void load_scenario_deadlock(System *sys) {
    init_system(sys, 2, 2);
    sys->allocation[0][0] = 1;
    sys->allocation[1][1] = 1;
    sys->request[0][1] = 1;
    sys->request[1][0] = 1;
    sys->max_need[0][0] = 1; sys->max_need[0][1] = 1;
    sys->max_need[1][0] = 1; sys->max_need[1][1] = 1;
    compute_need_matrix(sys);
    log_event(sys, "Loaded scenario: Deadlock (P0 holds R0 wants R1, P1 holds R1 wants R0)");
}

static void load_scenario_avoidance(System *sys) {
    load_scenario_safe(sys);
    log_event(sys, "Loaded scenario: Avoidance Demo - try P0 requesting 3 3 2");
}

static void load_scenario_philosophers(System *sys) {
    init_system(sys, 5, 5);
    for (int i = 0; i < 5; i++) {
        snprintf(sys->process_names[i], NAME_LEN, "Ph%d", i);
        snprintf(sys->resource_names[i], NAME_LEN, "F%d", i);
        sys->allocation[i][i] = 1;                 /* holds its left fork   */
        sys->request[i][(i + 1) % 5] = 1;          /* wants its right fork  */
        sys->max_need[i][i] = 1;
        sys->max_need[i][(i + 1) % 5] = 1;
    }
    compute_need_matrix(sys);
    log_event(sys, "Loaded scenario: Dining Philosophers - everyone holds one fork");
}

/* ---------- actions ---------- */

static void run_detection(System *sys) {
    int deadlocked[MAX_PROCESSES], count;
    int found = detect_deadlock(sys, deadlocked, &count);

    print_rag_edges(sys);

    int wfg[MAX_PROCESSES][MAX_PROCESSES];
    build_wait_for_graph(sys, wfg);
    int cycle_path[MAX_PROCESSES + 1], cycle_len = 0;
    detect_cycle_wfg(wfg, sys->num_processes, cycle_path, &cycle_len);
    print_wfg(wfg, sys, cycle_path, cycle_len);

    printf("\n-- Detection Result (matrix-based algorithm) --\n");
    if (found) {
        printf("  DEADLOCK DETECTED. Processes stuck: ");
        for (int i = 0; i < count; i++) printf("%s ", sys->process_names[deadlocked[i]]);
        printf("\n");
        log_event(sys, "Detection: DEADLOCK found (%d process%s stuck)", count, count == 1 ? "" : "es");
    } else {
        printf("  No deadlock. All processes can eventually complete.\n");
        log_event(sys, "Detection: no deadlock");
    }
}

static void run_safety_check(System *sys) {
    int seq[MAX_PROCESSES];
    print_available(sys);
    if (is_safe_state(sys, seq)) {
        printf("\nSystem is in a SAFE state.\nSafe sequence: ");
        for (int i = 0; i < sys->num_processes; i++)
            printf("%s%s", sys->process_names[seq[i]], (i < sys->num_processes - 1) ? " -> " : "\n");
        log_event(sys, "Safety check: SAFE");
    } else {
        printf("\nSystem is in an UNSAFE state (no safe sequence exists).\n");
        log_event(sys, "Safety check: UNSAFE");
    }
}

static void run_request(System *sys) {
    int pid;
    char prompt[64];
    snprintf(prompt, sizeof(prompt), "Enter process index (0 to %d): ", sys->num_processes - 1);
    if (read_int(prompt, &pid) != 1 || pid < 0 || pid >= sys->num_processes) {
        printf("Invalid process index.\n");
        return;
    }

    int req[MAX_RESOURCES];
    printf("Enter request amount for each of %d resources:\n", sys->num_resources);
    for (int j = 0; j < sys->num_resources; j++) {
        char p2[40];
        snprintf(p2, sizeof(p2), "  %s: ", sys->resource_names[j]);
        if (read_int(p2, &req[j]) != 1 || req[j] < 0) {
            printf("Invalid input.\n");
            return;
        }
    }

    char reason[128], vec[64];
    int code = request_resources(sys, pid, req, reason, sizeof(reason));
    fmt_vec(req, sys->num_resources, vec, sizeof(vec));
    printf("\nResult: %s\n", reason);
    printf("Status: %s\n", code == REQ_GRANTED ? "GRANTED" :
                           code == REQ_PENDING ? "DENIED (process waits)" : "REJECTED");
    log_event(sys, "%s requests [%s]: %s", sys->process_names[pid], vec, reason);
}

static void run_kill(System *sys) {
    int pid;
    if (read_int("Enter process index to kill: ", &pid) != 1 ||
        pid < 0 || pid >= sys->num_processes) {
        printf("Invalid process index.\n");
        return;
    }
    kill_process(sys, pid);
    log_event(sys, "Recovery: killed %s, all its resources released", sys->process_names[pid]);
}

static void run_preempt(System *sys) {
    int pid, rid, count;
    if (read_int("Enter process index: ", &pid) != 1 ||
        read_int("Enter resource index: ", &rid) != 1 ||
        read_int("Enter amount to preempt: ", &count) != 1 ||
        pid < 0 || pid >= sys->num_processes ||
        rid < 0 || rid >= sys->num_resources) {
        printf("Invalid input.\n");
        return;
    }
    if (preempt_resource(sys, pid, rid, count))
        log_event(sys, "Recovery: preempted %d x %s from %s",
                  count, sys->resource_names[rid], sys->process_names[pid]);
}

static void toggle_policies(System *sys) {
    int c;
    printf("Current policies:\n");
    printf("  1. No Hold-and-Wait  : %s\n", sys->policy_no_hold_and_wait ? "ON" : "OFF");
    printf("  2. Resource Ordering : %s\n", sys->policy_resource_ordering ? "ON" : "OFF");
    if (read_int("Toggle which policy? (1/2, 0 to cancel): ", &c) != 1) return;
    if (c == 1) {
        sys->policy_no_hold_and_wait = !sys->policy_no_hold_and_wait;
        log_event(sys, "Policy No-Hold-and-Wait -> %s", sys->policy_no_hold_and_wait ? "ON" : "OFF");
    } else if (c == 2) {
        sys->policy_resource_ordering = !sys->policy_resource_ordering;
        log_event(sys, "Policy Resource-Ordering -> %s", sys->policy_resource_ordering ? "ON" : "OFF");
    }
    printf("Updated.\n");
}

/* Processes actually RUN along the safe sequence: each one acquires its
 * remaining need, then finishes and releases everything. The live dashboard
 * updates after every step so you can watch the allocation change. */
static void run_live_simulation(System *sys, int delay_ms) {
    int seq[MAX_PROCESSES];
    if (!is_safe_state(sys, seq)) {
        printf("Cannot run: the current state is UNSAFE (no safe sequence).\n");
        log_event(sys, "Simulation refused: state is unsafe");
        return;
    }

    int active = 0;
    for (int i = 0; i < sys->num_processes; i++)
        if (sys->pstate[i] == PROC_ACTIVE) active++;
    if (active == 0) { printf("No active processes left to run.\n"); return; }

    printf("\n--- Live simulation started (watch the dashboard) ---\n");
    log_event(sys, "Simulation started: %d process(es) will run in safe order", active);
    sync_view(sys);
    sleep_ms(delay_ms);

    int step = 0;
    for (int k = 0; k < sys->num_processes; k++) {
        int pid = seq[k];
        if (sys->pstate[pid] != PROC_ACTIVE) continue;
        step++;
        char vec[64];

        /* 1. acquire the remaining need */
        for (int j = 0; j < sys->num_resources; j++)
            if (sys->need[pid][j] > sys->available[j]) {
                printf("Internal error: need exceeds available. Stopping.\n");
                return;
            }
        fmt_vec(sys->need[pid], sys->num_resources, vec, sizeof(vec));
        printf("[%d/%d] %s acquires its remaining need [%s] and starts running\n",
               step, active, sys->process_names[pid], vec);
        log_event(sys, "%s acquires remaining need [%s] and runs", sys->process_names[pid], vec);
        for (int j = 0; j < sys->num_resources; j++) {
            sys->available[j]     -= sys->need[pid][j];
            sys->allocation[pid][j] += sys->need[pid][j];
            sys->need[pid][j]      = 0;
            sys->request[pid][j]   = 0;
        }
        sync_view(sys);
        sleep_ms(delay_ms);

        /* 2. finish and release everything */
        fmt_vec(sys->allocation[pid], sys->num_resources, vec, sizeof(vec));
        printf("[%d/%d] %s finished, releases [%s]\n", step, active, sys->process_names[pid], vec);
        log_event(sys, "%s finished and released [%s]", sys->process_names[pid], vec);
        for (int j = 0; j < sys->num_resources; j++) {
            sys->available[j]  += sys->allocation[pid][j];
            sys->allocation[pid][j] = 0;
            sys->max_need[pid][j]   = 0;
            sys->need[pid][j]       = 0;
        }
        sys->pstate[pid] = PROC_FINISHED;
        sync_view(sys);
        sleep_ms(delay_ms);
    }
    printf("--- Simulation complete: every process finished, no deadlock ---\n");
    log_event(sys, "Simulation complete: all resources returned");
}

static void print_menu(void) {
    printf("\n==================== DEADLOCK SIMULATOR ====================\n");
    printf(" Scenario Presets:\n");
    printf("  1. Load Safe-State Scenario\n");
    printf("  2. Load Deadlock Scenario\n");
    printf("  3. Load Avoidance-Demo Scenario\n");
    printf(" Actions:\n");
    printf("  4. View Matrices\n");
    printf("  5. Check Safe State (Banker's Safety Algorithm)\n");
    printf("  6. Run Deadlock Detection\n");
    printf("  7. Make a Resource Request (Banker's Request Algorithm)\n");
    printf("  8. Kill a Process (Recovery)\n");
    printf("  9. Preempt Resources from a Process (Recovery)\n");
    printf(" 10. Toggle Prevention Policies\n");
    printf(" 11. Export a JSON snapshot now (live file is updated automatically)\n");
    printf(" Live Demo:\n");
    printf(" 12. Run Live Simulation (processes run & release along safe sequence)\n");
    printf(" 13. Load Dining-Philosophers Deadlock (5-process circular wait)\n");
    printf("  0. Exit\n");
    printf("==============================================================\n");
    printf("Live dashboard: run  python visualizer/live_view.py  in a 2nd terminal\n");
    printf("Choice: ");
    fflush(stdout);
}

int main(void) {
    System sys;
    load_scenario_safe(&sys);
    sync_view(&sys);

    printf("Deadlock Detection & Prevention Simulator\n");
    printf("Loaded default scenario: Safe-State (Scenario 1)\n");

    int choice = -1;
    do {
        print_menu();
        int r = read_int(NULL, &choice);
        if (r == -1) break;
        if (r == 0) { printf("Invalid input.\n"); choice = -1; continue; }

        switch (choice) {
            case 1:  load_scenario_safe(&sys);       printf("Loaded: Safe-State Scenario.\n"); break;
            case 2:  load_scenario_deadlock(&sys);   printf("Loaded: Deadlock Scenario (P0/P1 circular wait).\n"); break;
            case 3:  load_scenario_avoidance(&sys);
                     printf("Loaded: Avoidance-Demo Scenario. Try option 7 with P0 requesting 3 3 2.\n"); break;
            case 4:  print_matrices(&sys); break;
            case 5:  run_safety_check(&sys); break;
            case 6:  run_detection(&sys); break;
            case 7:  run_request(&sys); break;
            case 8:  run_kill(&sys); break;
            case 9:  run_preempt(&sys); break;
            case 10: toggle_policies(&sys); break;
            case 11: export_state_json(&sys, STATE_FILE, 0); break;
            case 12: {
                int d;
                if (read_int("Delay between steps in ms (e.g. 1500): ", &d) != 1 || d < 0 || d > 10000)
                    printf("Invalid delay.\n");
                else
                    run_live_simulation(&sys, d);
                break;
            }
            case 13: load_scenario_philosophers(&sys);
                     printf("Loaded: Dining Philosophers (5-process deadlock cycle).\n"); break;
            case 0:  printf("Exiting. Goodbye.\n"); break;
            default: printf("Invalid choice.\n");
        }
        if (choice != 0) sync_view(&sys);
    } while (choice != 0);

    return 0;
}

#include <stdio.h>
#include <string.h>
#include "bankers.h"
#include "prevention.h"

int is_safe_state(System *sys, int safe_seq[]) {
    int work[MAX_RESOURCES];
    int finish[MAX_PROCESSES];

    for (int j = 0; j < sys->num_resources; j++)
        work[j] = sys->available[j];
    for (int i = 0; i < sys->num_processes; i++)
        finish[i] = 0;

    int seq_count = 0;

    while (seq_count < sys->num_processes) {
        int found = 0;

        for (int i = 0; i < sys->num_processes; i++) {
            if (finish[i]) continue;

            int can_allocate = 1;
            for (int j = 0; j < sys->num_resources; j++) {
                if (sys->need[i][j] > work[j]) {
                    can_allocate = 0;
                    break;
                }
            }

            if (can_allocate) {
                for (int j = 0; j < sys->num_resources; j++)
                    work[j] += sys->allocation[i][j];
                finish[i] = 1;
                safe_seq[seq_count++] = i;
                found = 1;
            }
        }

        if (!found) return 0;
    }

    return 1;
}

static void set_pending(System *sys, int pid, const int request[]) {
    for (int j = 0; j < sys->num_resources; j++)
        sys->request[pid][j] = request[j];
}

static void clear_pending(System *sys, int pid) {
    for (int j = 0; j < sys->num_resources; j++)
        sys->request[pid][j] = 0;
}

int request_resources(System *sys, int pid, int request[], char *reason_out, int reason_len) {
    if (sys->pstate[pid] != PROC_ACTIVE) {
        snprintf(reason_out, reason_len, "Rejected: %s is no longer active", sys->process_names[pid]);
        return REQ_REJECTED;
    }

    int total = 0;
    for (int j = 0; j < sys->num_resources; j++) total += request[j];
    if (total == 0) {
        snprintf(reason_out, reason_len, "Rejected: empty request");
        return REQ_REJECTED;
    }

    /* Prevention policy checks first (only if enabled) */
    if (sys->policy_no_hold_and_wait && !check_hold_and_wait(sys, pid)) {
        snprintf(reason_out, reason_len,
                 "Denied by policy: no-hold-and-wait (process already holds resources)");
        return REQ_REJECTED;
    }
    if (sys->policy_resource_ordering) {
        for (int j = 0; j < sys->num_resources; j++) {
            if (request[j] > 0 && !check_circular_wait(sys, pid, j)) {
                snprintf(reason_out, reason_len,
                         "Denied by policy: resource-ordering violated for %s",
                         sys->resource_names[j]);
                return REQ_REJECTED;
            }
        }
    }

    /* Step 1: request <= need ? */
    for (int j = 0; j < sys->num_resources; j++) {
        if (request[j] > sys->need[pid][j]) {
            snprintf(reason_out, reason_len,
                     "Error: request exceeds declared max need for %s", sys->resource_names[j]);
            return REQ_REJECTED;
        }
    }
    /* Step 2: request <= available ? otherwise the process waits */
    for (int j = 0; j < sys->num_resources; j++) {
        if (request[j] > sys->available[j]) {
            set_pending(sys, pid, request);
            snprintf(reason_out, reason_len,
                     "Process must wait: not enough available %s", sys->resource_names[j]);
            return REQ_PENDING;
        }
    }

    /* Step 3: pretend to allocate, then run the safety algorithm */
    for (int j = 0; j < sys->num_resources; j++) {
        sys->available[j] -= request[j];
        sys->allocation[pid][j] += request[j];
        sys->need[pid][j] -= request[j];
    }

    int safe_seq[MAX_PROCESSES];
    if (is_safe_state(sys, safe_seq)) {
        clear_pending(sys, pid);
        snprintf(reason_out, reason_len, "Granted - system remains in a safe state");
        return REQ_GRANTED;
    }

    for (int j = 0; j < sys->num_resources; j++) {
        sys->available[j] += request[j];
        sys->allocation[pid][j] -= request[j];
        sys->need[pid][j] += request[j];
    }
    set_pending(sys, pid, request);
    snprintf(reason_out, reason_len, "Denied - granting would leave system unsafe");
    return REQ_PENDING;
}

#ifndef MODELS_H
#define MODELS_H

#define MAX_PROCESSES 10
#define MAX_RESOURCES 10
#define NAME_LEN 12
#define MAX_EVENTS 8
#define EVENT_LEN 160

/* Process lifecycle states (shown live in the visualizer) */
#define PROC_ACTIVE   0
#define PROC_FINISHED 1
#define PROC_KILLED   2

typedef struct {
    int num_processes;
    int num_resources;

    int allocation[MAX_PROCESSES][MAX_RESOURCES];
    int max_need[MAX_PROCESSES][MAX_RESOURCES];
    int need[MAX_PROCESSES][MAX_RESOURCES];
    int request[MAX_PROCESSES][MAX_RESOURCES];   /* pending (waiting) requests */

    int available[MAX_RESOURCES];
    int total_instances[MAX_RESOURCES];

    char process_names[MAX_PROCESSES][NAME_LEN];
    char resource_names[MAX_RESOURCES][NAME_LEN];
    int  pstate[MAX_PROCESSES];

    /* Prevention policy flags */
    int policy_no_hold_and_wait;
    int policy_resource_ordering;
    int resource_order[MAX_RESOURCES];

    /* Rolling event log, shown in the live dashboard */
    char events[MAX_EVENTS][EVENT_LEN];
    int  event_count;

    char label[96];            /* shown in the dashboard footer */
} System;

void init_system(System *sys, int num_p, int num_r);
void compute_need_matrix(System *sys);
void print_matrices(System *sys);
void print_available(System *sys);

/* printf-style: appends a message to the event log */
void log_event(System *sys, const char *fmt, ...);

#endif

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "models.h"

void init_system(System *sys, int num_p, int num_r) {
    sys->num_processes = num_p;
    sys->num_resources = num_r;

    memset(sys->allocation, 0, sizeof(sys->allocation));
    memset(sys->max_need, 0, sizeof(sys->max_need));
    memset(sys->need, 0, sizeof(sys->need));
    memset(sys->request, 0, sizeof(sys->request));
    memset(sys->available, 0, sizeof(sys->available));
    memset(sys->total_instances, 0, sizeof(sys->total_instances));
    memset(sys->pstate, 0, sizeof(sys->pstate));
    memset(sys->events, 0, sizeof(sys->events));
    sys->event_count = 0;
    snprintf(sys->label, sizeof(sys->label), "Interactive Simulator");

    sys->policy_no_hold_and_wait = 0;
    sys->policy_resource_ordering = 0;

    for (int i = 0; i < num_p; i++)
        snprintf(sys->process_names[i], NAME_LEN, "P%d", i);
    for (int j = 0; j < num_r; j++) {
        snprintf(sys->resource_names[j], NAME_LEN, "R%d", j);
        sys->resource_order[j] = j;
    }
}

void compute_need_matrix(System *sys) {
    for (int i = 0; i < sys->num_processes; i++)
        for (int j = 0; j < sys->num_resources; j++)
            sys->need[i][j] = sys->max_need[i][j] - sys->allocation[i][j];
}

void log_event(System *sys, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(sys->events[sys->event_count % MAX_EVENTS], EVENT_LEN, fmt, ap);
    va_end(ap);
    sys->event_count++;
}

static void print_row_header(System *sys) {
    printf("%-6s", "");
    for (int j = 0; j < sys->num_resources; j++)
        printf("%-6s", sys->resource_names[j]);
    printf("\n");
}

static void print_one(System *sys, const char *title, int m[MAX_PROCESSES][MAX_RESOURCES]) {
    printf("\n-- %s --\n", title);
    print_row_header(sys);
    for (int i = 0; i < sys->num_processes; i++) {
        printf("%-6s", sys->process_names[i]);
        for (int j = 0; j < sys->num_resources; j++)
            printf("%-6d", m[i][j]);
        printf("\n");
    }
}

void print_matrices(System *sys) {
    printf("\n===================== SYSTEM STATE =====================\n");
    print_one(sys, "Allocation", sys->allocation);
    print_one(sys, "Max Need", sys->max_need);
    print_one(sys, "Need (Max - Allocation)", sys->need);
    print_one(sys, "Pending Requests", sys->request);
    print_available(sys);
    printf("==========================================================\n");
}

void print_available(System *sys) {
    printf("\n-- Available --\n");
    print_row_header(sys);
    printf("%-6s", "");
    for (int j = 0; j < sys->num_resources; j++)
        printf("%-6d", sys->available[j]);
    printf("\n");
}

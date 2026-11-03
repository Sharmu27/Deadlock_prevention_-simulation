#include <stdio.h>
#include "visualize.h"

void print_rag_edges(System *sys) {
    printf("\n-- Resource Allocation Graph (text form) --\n");
    int any = 0;

    for (int i = 0; i < sys->num_processes; i++) {
        for (int j = 0; j < sys->num_resources; j++) {
            if (sys->allocation[i][j] > 0) {
                printf("  %s --assigned-->  %s  (x%d)\n",
                       sys->resource_names[j], sys->process_names[i], sys->allocation[i][j]);
                any = 1;
            }
            if (sys->request[i][j] > 0) {
                printf("  %s --requests-->  %s  (x%d)\n",
                       sys->process_names[i], sys->resource_names[j], sys->request[i][j]);
                any = 1;
            }
        }
    }
    if (!any) printf("  (no active allocations or requests)\n");
}

static int in_cycle(int pid, int cycle_path[], int cycle_len) {
    for (int i = 0; i < cycle_len; i++)
        if (cycle_path[i] == pid) return 1;
    return 0;
}

void print_wfg(int wfg[MAX_PROCESSES][MAX_PROCESSES], System *sys,
                int cycle_path[], int cycle_len) {
    printf("\n-- Wait-For Graph --\n");
    int any = 0;

    for (int i = 0; i < sys->num_processes; i++) {
        for (int k = 0; k < sys->num_processes; k++) {
            if (wfg[i][k]) {
                int marked = cycle_len > 0 && in_cycle(i, cycle_path, cycle_len) && in_cycle(k, cycle_path, cycle_len);
                printf("  %s --waits for--> %s%s\n",
                       sys->process_names[i], sys->process_names[k],
                       marked ? "   [CYCLE EDGE]" : "");
                any = 1;
            }
        }
    }
    if (!any) printf("  (no process is currently waiting)\n");

    if (cycle_len > 0) {
        printf("\n  Cycle path: ");
        for (int i = 0; i < cycle_len; i++) {
            printf("%s", sys->process_names[cycle_path[i]]);
            if (i < cycle_len - 1) printf(" -> ");
        }
        printf("\n");
    }
}

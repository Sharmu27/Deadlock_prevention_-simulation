#include <stdio.h>
#include <string.h>
#include "detection.h"

int detect_deadlock(System *sys, int deadlocked[], int *count) {
    int work[MAX_RESOURCES];
    int finish[MAX_PROCESSES];

    for (int j = 0; j < sys->num_resources; j++)
        work[j] = sys->available[j];
    for (int i = 0; i < sys->num_processes; i++)
        finish[i] = 0;

    int changed = 1;
    while (changed) {
        changed = 0;
        for (int i = 0; i < sys->num_processes; i++) {
            if (finish[i]) continue;

            int can_proceed = 1;
            for (int j = 0; j < sys->num_resources; j++) {
                if (sys->request[i][j] > work[j]) {
                    can_proceed = 0;
                    break;
                }
            }

            if (can_proceed) {
                for (int j = 0; j < sys->num_resources; j++)
                    work[j] += sys->allocation[i][j];
                finish[i] = 1;
                changed = 1;
            }
        }
    }

    *count = 0;
    for (int i = 0; i < sys->num_processes; i++) {
        if (!finish[i]) {
            deadlocked[(*count)++] = i;
        }
    }

    return (*count > 0);
}

void build_wait_for_graph(System *sys, int wfg[MAX_PROCESSES][MAX_PROCESSES]) {
    memset(wfg, 0, sizeof(int) * MAX_PROCESSES * MAX_PROCESSES);

    for (int i = 0; i < sys->num_processes; i++) {
        for (int j = 0; j < sys->num_resources; j++) {
            if (sys->request[i][j] > 0) {
                /* process i wants resource j - find who holds it */
                for (int k = 0; k < sys->num_processes; k++) {
                    if (k != i && sys->allocation[k][j] > 0) {
                        wfg[i][k] = 1;
                    }
                }
            }
        }
    }
}

/* DFS helper: returns 1 if a cycle is found starting from `node`.
 * path[] tracks the current DFS stack so we can report the exact cycle. */
static int dfs_visit(int wfg[MAX_PROCESSES][MAX_PROCESSES], int n, int node,
                      int *visited, int *on_stack, int *path, int *path_len,
                      int cycle_path[], int *cycle_len) {
    visited[node] = 1;
    on_stack[node] = 1;
    path[(*path_len)++] = node;

    for (int i = 0; i < n; i++) {
        if (!wfg[node][i]) continue;

        if (!visited[i]) {
            if (dfs_visit(wfg, n, i, visited, on_stack, path, path_len, cycle_path, cycle_len))
                return 1;
        } else if (on_stack[i]) {
            /* found a back edge to node i, still on the stack -> cycle.
             * Extract the cycle portion of path[] starting from i. */
            int start = 0;
            for (int p = 0; p < *path_len; p++) {
                if (path[p] == i) { start = p; break; }
            }
            *cycle_len = 0;
            for (int p = start; p < *path_len; p++)
                cycle_path[(*cycle_len)++] = path[p];
            cycle_path[(*cycle_len)++] = i; /* close the cycle */
            return 1;
        }
    }

    on_stack[node] = 0;
    (*path_len)--;
    return 0;
}

int detect_cycle_wfg(int wfg[MAX_PROCESSES][MAX_PROCESSES], int n,
                      int cycle_path[], int *cycle_len) {
    int visited[MAX_PROCESSES] = {0};
    int on_stack[MAX_PROCESSES] = {0};
    int path[MAX_PROCESSES];
    int path_len = 0;

    *cycle_len = 0;

    for (int i = 0; i < n; i++) {
        if (!visited[i]) {
            path_len = 0;
            if (dfs_visit(wfg, n, i, visited, on_stack, path, &path_len, cycle_path, cycle_len))
                return 1;
        }
    }
    return 0;
}

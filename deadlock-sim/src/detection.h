#ifndef DETECTION_H
#define DETECTION_H
#include "models.h"

/* Multi-instance matrix-based detection algorithm.
 * Returns 1 if deadlock found, 0 otherwise.
 * Fills deadlocked[] with indices of deadlocked processes, sets *count. */
int detect_deadlock(System *sys, int deadlocked[], int *count);

/* Builds a Wait-For Graph from current allocation + request state.
 * wfg[i][j] = 1 means process i is waiting on a resource held by process j. */
void build_wait_for_graph(System *sys, int wfg[MAX_PROCESSES][MAX_PROCESSES]);

/* Runs DFS-based cycle detection on the Wait-For Graph.
 * Returns 1 if a cycle exists, and fills cycle_path[] / *cycle_len with the
 * sequence of process indices forming the cycle. */
int detect_cycle_wfg(int wfg[MAX_PROCESSES][MAX_PROCESSES], int n,
                      int cycle_path[], int *cycle_len);

#endif

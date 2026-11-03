#include <stdio.h>
#include "recovery.h"

void kill_process(System *sys, int pid) {
    for (int j = 0; j < sys->num_resources; j++) {
        sys->available[j] += sys->allocation[pid][j];
        sys->allocation[pid][j] = 0;
        sys->request[pid][j] = 0;
        sys->max_need[pid][j] = 0;   /* a terminated process needs nothing */
        sys->need[pid][j] = 0;
    }
    sys->pstate[pid] = PROC_KILLED;
    printf("-> Process %s killed. Its resources have been released.\n", sys->process_names[pid]);
}

int preempt_resource(System *sys, int pid, int rid, int count) {
    if (count <= 0 || sys->allocation[pid][rid] < count) {
        printf("-> Error: %s only holds %d units of %s, cannot preempt %d.\n",
               sys->process_names[pid], sys->allocation[pid][rid], sys->resource_names[rid], count);
        return 0;
    }
    sys->allocation[pid][rid] -= count;
    sys->need[pid][rid] += count;    /* it will have to ask for it again */
    sys->available[rid] += count;
    printf("-> Preempted %d unit(s) of %s from %s.\n", count, sys->resource_names[rid], sys->process_names[pid]);
    return 1;
}

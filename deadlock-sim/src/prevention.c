#include "prevention.h"

int check_hold_and_wait(System *sys, int pid) {
    for (int j = 0; j < sys->num_resources; j++) {
        if (sys->allocation[pid][j] > 0) {
            return 0;
        }
    }
    return 1;
}

int check_circular_wait(System *sys, int pid, int rid) {
    for (int j = 0; j < sys->num_resources; j++) {
        if (sys->allocation[pid][j] > 0 && sys->resource_order[j] > sys->resource_order[rid]) {
            return 0;
        }
    }
    return 1;
}

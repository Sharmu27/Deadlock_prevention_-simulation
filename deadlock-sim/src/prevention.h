#ifndef PREVENTION_H
#define PREVENTION_H
#include "models.h"

/* Returns 1 if the process is allowed to make a new request under the
 * no-hold-and-wait policy (i.e. it currently holds nothing). */
int check_hold_and_wait(System *sys, int pid);

/* Returns 1 if requesting resource `rid` does not violate the global
 * resource-ordering rule (a process holding a higher-ranked resource
 * cannot request a lower-ranked one - this breaks circular wait). */
int check_circular_wait(System *sys, int pid, int rid);

#endif

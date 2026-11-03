#ifndef RECOVERY_H
#define RECOVERY_H
#include "models.h"

/* Kills a process: releases all its allocated resources back to Available. */
void kill_process(System *sys, int pid);

/* Preempts `count` units of resource `rid` from process `pid`. */
int preempt_resource(System *sys, int pid, int rid, int count);

#endif

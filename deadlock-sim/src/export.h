#ifndef EXPORT_H
#define EXPORT_H
#include "models.h"

/* Writes the full system state AND the algorithm results (safe/unsafe, deadlock,
 * cycle, wait-for graph, event log) to a JSON file for the live dashboard.
 * quiet = 1 suppresses the console message. */
void export_state_json(System *sys, const char *filepath, int quiet);

#endif

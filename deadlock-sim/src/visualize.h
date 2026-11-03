#ifndef VISUALIZE_H
#define VISUALIZE_H
#include "models.h"

/* Prints the Resource Allocation Graph as text edges:
 *   Pi --request--> Rj   and   Rj --assigned--> Pi
 */
void print_rag_edges(System *sys);

/* Prints the Wait-For Graph as text edges: Pi --waits for--> Pk
 * Highlights the given cycle path if one was found. */
void print_wfg(int wfg[MAX_PROCESSES][MAX_PROCESSES], System *sys,
                int cycle_path[], int cycle_len);

#endif

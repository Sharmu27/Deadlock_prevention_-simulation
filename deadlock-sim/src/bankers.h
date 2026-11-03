#ifndef BANKERS_H
#define BANKERS_H
#include "models.h"

#define REQ_GRANTED   1   /* resources allocated, state still safe            */
#define REQ_PENDING   0   /* denied for now: process must wait (shown live)   */
#define REQ_REJECTED -1   /* invalid / blocked by a prevention policy         */

/* Safety algorithm. Returns 1 if safe (fills safe_seq[]), 0 if unsafe. */
int is_safe_state(System *sys, int safe_seq[]);

/* Resource-request algorithm. Returns REQ_GRANTED / REQ_PENDING / REQ_REJECTED.
 * Allocation state is fully rolled back unless the request is granted. */
int request_resources(System *sys, int pid, int request[], char *reason_out, int reason_len);

#endif

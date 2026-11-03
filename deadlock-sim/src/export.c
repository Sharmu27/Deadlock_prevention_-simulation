                                                                    #include <stdio.h>
#include "export.h"
#include "detection.h"
#include "bankers.h"

static unsigned long g_version = 0;

static void put_ints(FILE *f, const int *a, int n) {
    fputc('[', f);
    for (int i = 0; i < n; i++) fprintf(f, "%s%d", i ? ", " : "", a[i]);
    fputc(']', f);
}

static void put_string(FILE *f, const char *s) {
    fputc('"', f);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { fputc('\\', f); fputc(c, f); }
        else if (c < 32) fputc(' ', f);
        else fputc(c, f);
    }
    fputc('"', f);
}

static void put_matrix(FILE *f, const char *key, const int *m, int stride, int rows, int cols) {
    fprintf(f, "  \"%s\": [\n", key);
    for (int r = 0; r < rows; r++) {
        fputs("    ", f);
        put_ints(f, m + r * stride, cols);
        fprintf(f, "%s\n", r < rows - 1 ? "," : "");
    }
    fputs("  ],\n", f);
}

static void write_json(System *sys, FILE *f) {
    int n = sys->num_processes, m = sys->num_resources;

    int deadlocked[MAX_PROCESSES], dcount = 0;
    int deadlock = detect_deadlock(sys, deadlocked, &dcount);

    int wfg[MAX_PROCESSES][MAX_PROCESSES];
    build_wait_for_graph(sys, wfg);
    int cycle[MAX_PROCESSES + 1], clen = 0;
    detect_cycle_wfg(wfg, n, cycle, &clen);

    int seq[MAX_PROCESSES];
    int safe = is_safe_state(sys, seq);

    int total[MAX_RESOURCES];
    for (int j = 0; j < m; j++) {
        total[j] = sys->available[j];
        for (int i = 0; i < n; i++) total[j] += sys->allocation[i][j];
    }

    fputs("{\n", f);
    fprintf(f, "  \"version\": %lu,\n", ++g_version);
    fputs("  \"label\": ", f); put_string(f, sys->label); fputs(",\n", f);
    fprintf(f, "  \"num_processes\": %d,\n  \"num_resources\": %d,\n", n, m);

    fputs("  \"process_names\": [", f);
    for (int i = 0; i < n; i++) { if (i) fputs(", ", f); put_string(f, sys->process_names[i]); }
    fputs("],\n  \"resource_names\": [", f);
    for (int j = 0; j < m; j++) { if (j) fputs(", ", f); put_string(f, sys->resource_names[j]); }
    fputs("],\n", f);

    fputs("  \"process_state\": ", f); put_ints(f, sys->pstate, n); fputs(",\n", f);

    put_matrix(f, "allocation", &sys->allocation[0][0], MAX_RESOURCES, n, m);
    put_matrix(f, "max", &sys->max_need[0][0], MAX_RESOURCES, n, m);
    put_matrix(f, "need", &sys->need[0][0], MAX_RESOURCES, n, m);
    put_matrix(f, "request", &sys->request[0][0], MAX_RESOURCES, n, m);
    put_matrix(f, "wfg", &wfg[0][0], MAX_PROCESSES, n, n);

    fputs("  \"available\": ", f); put_ints(f, sys->available, m); fputs(",\n", f);
    fputs("  \"total\": ", f);     put_ints(f, total, m);           fputs(",\n", f);

    fprintf(f, "  \"deadlock\": %s,\n", deadlock ? "true" : "false");
    fputs("  \"deadlocked\": ", f); put_ints(f, deadlocked, dcount); fputs(",\n", f);
    fputs("  \"cycle\": ", f);      put_ints(f, cycle, clen);        fputs(",\n", f);
    fprintf(f, "  \"safe\": %s,\n", safe ? "true" : "false");
    fputs("  \"safe_sequence\": ", f); put_ints(f, seq, safe ? n : 0); fputs(",\n", f);

    fprintf(f, "  \"policies\": {\"no_hold_and_wait\": %d, \"resource_ordering\": %d},\n",
            sys->policy_no_hold_and_wait, sys->policy_resource_ordering);

    /* newest event first */
    int shown = sys->event_count < MAX_EVENTS ? sys->event_count : MAX_EVENTS;
    fputs("  \"events\": [", f);
    for (int k = 0; k < shown; k++) {
        if (k) fputs(", ", f);
        put_string(f, sys->events[(sys->event_count - 1 - k) % MAX_EVENTS]);
    }
    fputs("]\n}\n", f);
}

void export_state_json(System *sys, const char *filepath, int quiet) {
    char tmp[300];
    int done = 0;
    snprintf(tmp, sizeof(tmp), "%s.tmp", filepath);

    /* write to a temp file, then swap in, so the viewer never reads half a file */
    FILE *f = fopen(tmp, "w");
    if (f) {
        write_json(sys, f);
        fclose(f);
#ifdef _WIN32
        remove(filepath);
#endif
        if (rename(tmp, filepath) == 0) done = 1;
        else remove(tmp);
    }
    if (!done) {                       /* fallback: write directly */
        f = fopen(filepath, "w");
        if (!f) {
            if (!quiet) printf("Error: could not open %s for writing.\n", filepath);
            return;
        }
        write_json(sys, f);
        fclose(f);
    }
    if (!quiet) printf("-> Exported system state to %s\n", filepath);
}

# Deadlock Detection & Prevention Simulator

**CS23501 — Operating Systems**

A simulator that models deadlock detection, avoidance (Banker's Algorithm),
prevention, and recovery in a resource-allocation system. The core engine
is written in C; a Python module reads the exported system state and
renders the Resource Allocation Graph and Wait-For Graph visually.

## Team

| Name | Register No. |
|---|---|
| Sana Tasneem Azimudin | 2024503007 |
| Monisha G | 2024503043 |
| Sharmu R | 2024503055 |
| Priyatharshini M | 2024503501 |

## Project Structure

```
deadlock-sim/
├── src/                    C source (the simulator engine)
│   ├── models.c/.h          System state: Allocation/Max/Need/Available matrices
│   ├── detection.c/.h        Cycle detection (DFS) + matrix-based detection algorithm
│   ├── bankers.c/.h           Banker's safety algorithm + resource-request algorithm
│   ├── prevention.c/.h         No-Hold-and-Wait and Resource-Ordering policies
│   ├── recovery.c/.h            Kill-process and preempt-resource recovery
│   ├── visualize.c/.h            Terminal (text) graph rendering
│   ├── rm.c/.h                      Resource Manager used by the real threads
│   ├── threads.c/.h                  Portable thread/lock layer (Win32 + pthreads)
│   ├── workload.c                   Automatic Workload Lab (no menus)
│   ├── export.c/.h                 Writes system_state.json (state + results + events)
│   ├── util.c/.h                    sleep helper (Windows/Linux)
│   └── main.c                      Interactive menu + built-in scenarios
├── visualizer/              Python visualization
│   ├── live_view.py           LIVE dashboard (auto-updating window)
│   ├── visualize.py           Static PNG of the RAG + Wait-For Graph
│   └── requirements.txt
├── build.bat / run_demo.bat / start_live_view.bat   Windows helpers
├── Makefile
└── README.md
```

## What this project is

An **automatic testbed for deadlock handling**. Real operating-system threads
("jobs") compete for shared devices (printer, scanner, disk, network) through a
**Resource Manager** built on the detection / avoidance / prevention / recovery
algorithms. The same workload is run under five strategies, measured, and
compared automatically. A live dashboard shows every step.

| # | Strategy | What happens |
|---|---|---|
| 1 | No safeguard | Deadlock occurs and the system freezes forever |
| 2 | Detection + Recovery | Deadlock is allowed, a detector thread finds it, aborts a victim job, which restarts |
| 3 | Avoidance (Banker's) | A request is granted only if the system stays in a safe state |
| 4 | Prevention: resource ordering | Devices must be acquired in a fixed order (breaks circular wait) |
| 5 | Prevention: no hold-and-wait | A job takes everything it needs at once, or nothing |

## Run it (one click, no typing)

```
build.bat          compile both programs (first time only)
run_demo.bat       opens the live dashboard AND runs the whole lab automatically
```

`run_demo.bat` runs three phases by itself:

1. **Classic scenario** - 5 jobs, a built-in circular wait, all 5 strategies, shown live.
2. **Stress test** - 8 randomly generated workloads x 5 strategies.
3. **Verdict** - computed from the measured results (which strategies froze,
   how much work was wasted, which safe strategy was fastest).

Results are also saved to `workload_results.csv` (open in Excel for the report).

Options: `workload.exe fast` (quicker), `workload.exe quick` (skip stress test),
`workload.exe 20` (20 random workloads).

Manual way: run `workload.exe` in one window and
`python visualizer\live_view.py` in another.

Linux / macOS / MSYS2: `make` then `./workload`.

## Optional: interactive explorer

`simulator.exe` is a menu-driven tool for exploring single scenarios by hand
(load a state, check safety, make a request, kill a process). It writes to the
same live dashboard. Do not run it at the same time as `workload.exe`.

## How the live dashboard works

Every state change rewrites `system_state.json` (matrices, safe/unsafe, deadlock,
cycle, wait-for graph, event log, and the active strategy). `visualizer/live_view.py`
watches that file and redraws automatically. It shows a status banner, the resource
allocation graph (one dot per device instance), the wait-for graph with the
deadlock cycle in red, live allocation bars, a process table and an event log.
A gold outline marks whatever just changed.

`visualizer/visualize.py` (static PNG for reports) still works with the same file.

## Algorithms Implemented

**Deadlock Detection (multi-instance)** — `detection.c: detect_deadlock()`
Matrix-based algorithm using Work/Finish vectors, identical in structure to
Banker's safety check but evaluated against `Request` rather than `Need`.
Complexity: O(n² × m) where n = processes, m = resource types.

**Deadlock Detection (single-instance)** — `detection.c: detect_cycle_wfg()`
Builds a Wait-For Graph and runs DFS with a recursion-stack check to find
back edges (cycles). Complexity: O(V + E).

**Banker's Algorithm** — `bankers.c`
- `is_safe_state()`: the safety algorithm, finds a process finish order
  such that every process can eventually obtain its maximum need.
- `request_resources()`: the resource-request algorithm — tentatively
  grants a request, re-checks safety, and rolls back if unsafe.

**Prevention** — `prevention.c`
- No-Hold-and-Wait: rejects any new request from a process that already
  holds resources (forces all-at-once allocation).
- Resource-Ordering: assigns each resource a rank; a process holding a
  higher-ranked resource cannot request a lower-ranked one, which
  structurally eliminates circular wait.

**Recovery** — `recovery.c`
- `kill_process()`: terminates a process, releasing all its resources.
- `preempt_resource()`: forcibly reclaims a specific amount of a resource.

## Test Scenarios Verified

1. **Safe state** — classic 5-process/3-resource example; Banker's finds a
   valid safe sequence.
2. **Deadlock** — 2 processes in a circular wait over single-instance
   resources; both detection algorithms correctly flag it.
3. **Recovery** — killing one process in the deadlocked pair releases its
   resource and resolves the deadlock (re-verified by re-running detection).
4. **Avoidance** — a request that is within `Need` and within `Available`
   but would still strand the system is correctly denied by Banker's
   Algorithm.
5. **Prevention** — with the No-Hold-and-Wait policy on, a process already
   holding a resource is denied any further request.

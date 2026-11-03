"""
Deadlock Simulator - Python Visualizer
----------------------------------------
Reads system_state.json (exported by the C simulator, menu option 11)
and renders:
  1. The Resource Allocation Graph (RAG)
  2. The Wait-For Graph, with any cycle highlighted in red

Usage:
    python visualize.py [path_to_json]

If no path is given, defaults to ../system_state.json
"""

import json
import sys
import os
import networkx as nx
import matplotlib.pyplot as plt

TEAL = "#029E8C"
CORAL = "#E8604C"
CHARCOAL = "#2B2F36"
GRAY = "#8A8F98"


def load_state(path):
    if not os.path.exists(path):
        print(f"Error: could not find '{path}'.")
        print("Run the C simulator first and use menu option 11 to export state.")
        sys.exit(1)
    with open(path, "r") as f:
        return json.load(f)


def build_rag(state):
    """Builds a directed graph with process and resource nodes."""
    G = nx.DiGraph()
    procs = state["process_names"]
    ress = state["resource_names"]

    for p in procs:
        G.add_node(p, kind="process")
    for r in ress:
        G.add_node(r, kind="resource")

    for i, p in enumerate(procs):
        for j, r in enumerate(ress):
            if state["allocation"][i][j] > 0:
                G.add_edge(r, p, kind="assignment", weight=state["allocation"][i][j])
            if state["request"][i][j] > 0:
                G.add_edge(p, r, kind="request", weight=state["request"][i][j])
    return G


def build_wait_for_graph(state):
    """Collapses the RAG into a process-only Wait-For Graph."""
    G = nx.DiGraph()
    procs = state["process_names"]
    ress = state["resource_names"]
    G.add_nodes_from(procs)

    for i, p in enumerate(procs):
        for j, r in enumerate(ress):
            if state["request"][i][j] > 0:
                for k, p2 in enumerate(procs):
                    if k != i and state["allocation"][k][j] > 0:
                        G.add_edge(p, p2)
    return G


def find_cycle(G):
    """Returns list of nodes in a cycle if one exists, else None."""
    try:
        cycle_edges = nx.find_cycle(G, orientation="original")
        nodes = []
        for u, v, _ in cycle_edges:
            if u not in nodes:
                nodes.append(u)
        return nodes, cycle_edges
    except nx.NetworkXNoCycle:
        return None, None


def draw_rag(state, ax):
    G = build_rag(state)
    pos = nx.spring_layout(G, seed=42, k=1.2)

    process_nodes = [n for n, d in G.nodes(data=True) if d["kind"] == "process"]
    resource_nodes = [n for n, d in G.nodes(data=True) if d["kind"] == "resource"]

    nx.draw_networkx_nodes(G, pos, nodelist=process_nodes, node_color=TEAL,
                            node_shape="o", node_size=1400, ax=ax)
    nx.draw_networkx_nodes(G, pos, nodelist=resource_nodes, node_color=CORAL,
                            node_shape="s", node_size=1400, ax=ax)
    nx.draw_networkx_labels(G, pos, font_color="white", font_weight="bold", ax=ax)

    assignment_edges = [(u, v) for u, v, d in G.edges(data=True) if d["kind"] == "assignment"]
    request_edges = [(u, v) for u, v, d in G.edges(data=True) if d["kind"] == "request"]

    nx.draw_networkx_edges(G, pos, edgelist=assignment_edges, edge_color=CORAL,
                            arrows=True, arrowsize=20, width=2, ax=ax,
                            connectionstyle="arc3,rad=0.1")
    nx.draw_networkx_edges(G, pos, edgelist=request_edges, edge_color=TEAL,
                            arrows=True, arrowsize=20, width=2, ax=ax,
                            connectionstyle="arc3,rad=0.1")

    ax.set_title("Resource Allocation Graph", fontsize=13, fontweight="bold")
    ax.axis("off")

    # simple legend
    ax.plot([], [], color=CORAL, label="assignment (resource -> process)")
    ax.plot([], [], color=TEAL, label="request (process -> resource)")
    ax.legend(loc="lower center", fontsize=8, frameon=False)


def draw_wfg(state, ax):
    G = build_wait_for_graph(state)
    cycle_nodes, cycle_edges = find_cycle(G)

    if len(G.nodes) == 0:
        ax.text(0.5, 0.5, "No processes", ha="center", va="center")
        ax.axis("off")
        return

    pos = nx.spring_layout(G, seed=42, k=1.2)

    nx.draw_networkx_nodes(G, pos, node_color=TEAL, node_size=1400, ax=ax)
    nx.draw_networkx_labels(G, pos, font_color="white", font_weight="bold", ax=ax)

    normal_edges = list(G.edges())
    cycle_edge_set = set()
    if cycle_edges:
        cycle_edge_set = {(u, v) for u, v, _ in cycle_edges}
        normal_edges = [e for e in normal_edges if e not in cycle_edge_set]

    nx.draw_networkx_edges(G, pos, edgelist=normal_edges, edge_color=GRAY,
                            arrows=True, arrowsize=20, width=1.5, ax=ax,
                            connectionstyle="arc3,rad=0.15")
    if cycle_edge_set:
        nx.draw_networkx_edges(G, pos, edgelist=list(cycle_edge_set), edge_color=CORAL,
                                arrows=True, arrowsize=22, width=3, ax=ax,
                                connectionstyle="arc3,rad=0.15")

    title = "Wait-For Graph"
    if cycle_nodes:
        title += f"  \u2014  DEADLOCK: {' -> '.join(cycle_nodes + [cycle_nodes[0]])}"
        ax.set_title(title, fontsize=12, fontweight="bold", color=CORAL)
    else:
        title += "  \u2014  No cycle (safe)"
        ax.set_title(title, fontsize=12, fontweight="bold", color=TEAL)

    ax.axis("off")


def main():
    json_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join("..", "system_state.json")
    state = load_state(json_path)

    fig, axes = plt.subplots(1, 2, figsize=(13, 6))
    fig.patch.set_facecolor("white")

    draw_rag(state, axes[0])
    draw_wfg(state, axes[1])

    fig.suptitle("Deadlock Detection & Prevention Simulator", fontsize=15, fontweight="bold")
    plt.tight_layout()

    out_path = "graph_output.png"
    plt.savefig(out_path, dpi=150, bbox_inches="tight")
    print(f"Saved visualization to {out_path}")

    # Only pop up an interactive window if a display is available.
    if os.environ.get("DISPLAY") or sys.platform in ("win32", "darwin"):
        try:
            plt.show()
        except Exception:
            pass


if __name__ == "__main__":
    main()

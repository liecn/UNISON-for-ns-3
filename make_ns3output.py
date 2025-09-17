#!/usr/bin/env python3
"""
Parse grouped_flows.txt and emit UD and RDMA durations.

- UD  = (resp_recv - req_send) - (resp_send - req_recv)
- RDMA = rdma_recv - rdma_send

If the same reqId produces multiple sequences, suffix the 2nd+ occurrences with -1, -2, ...
Outputs are sorted by sequence timestamp so UD/RDMA are interleaved in time order.

Usage:
    python compute_durs.py [--input grouped_flows.txt] [--debug]
"""

import re
import collections
import sys

EVENT_RE = re.compile(
    r"\[(\w+)\s+([\w_]+)\]\s+t=(\d+)\s+ns\s+reqId=(\d+).*client_node_id=(\d+)"
)

def parse_file(path):
    flows = collections.defaultdict(list)  # reqId -> list of events (dict)
    with open(path, "r") as f:
        for line in f:
            m = EVENT_RE.search(line)
            if not m:
                continue
            side, event, t, reqId, client = m.groups()
            t = int(t)
            reqId = int(reqId)
            client = int(client)
            flows[reqId].append({
                "side": side,
                "event": event,
                "t": t,
                "client": client,
                "raw": line.rstrip("\n")
            })
    return flows

def extract_sequences(events, pattern_list):
    """
    Given events sorted by time, find non-overlapping sequences that match pattern_list.
    pattern_list is e.g. ['req_send','req_recv','resp_send','resp_recv'] or ['rdma_send','rdma_recv'].
    Returns a list of lists (each inner list contains event dicts in order).
    """
    seqs = []
    i = 0
    n = len(events)
    while i < n:
        found = []
        pos = i
        for want in pattern_list:
            j = pos
            while j < n and events[j]["event"] != want:
                j += 1
            if j == n:
                found = None
                break
            found.append((j, events[j]))
            pos = j + 1
        if not found:
            break
        seqs.append([e for idx,e in found])
        # advance past the last matched index to avoid overlapping sequences
        i = found[-1][0] + 1
    return seqs

def compute(flows, debug=False):
    # sort reqIds by first timestamp (keeps output in chronological order across reqIds)
    sorted_reqs = sorted(flows.items(), key=lambda kv: min(e['t'] for e in kv[1]))
    outputs = []   # will hold dicts with keys: type, client, id, dur, ts, details
    dup_counter = collections.defaultdict(int)  # counts how many sequences we've emitted for reqId

    for reqId, evs in sorted_reqs:
        evs_sorted = sorted(evs, key=lambda e: e['t'])
        # prefer client from a req_send event; fallback to any client_node_id
        client = None
        for e in evs_sorted:
            if e["event"] == "req_send":
                client = e["client"] - 1
                break
        if client is None and evs_sorted:
            client = evs_sorted[0]["client"] - 1
        if client is None:
            client = 0

        # find UD and RDMA sequences within this reqId
        ud_seqs = extract_sequences(evs_sorted, ["req_send", "req_recv", "resp_send", "resp_recv"])
        rdma_seqs = extract_sequences(evs_sorted, ["rdma_send", "rdma_recv"])

        # Emit UD sequences
        for seq in ud_seqs:
            rs = seq[0]["t"]   # req_send
            rr = seq[1]["t"]   # req_recv
            rps = seq[2]["t"]  # resp_send
            rpr = seq[3]["t"]  # resp_recv
            ud = (rpr - rs) - (rps - rr)
            suffix = "" if dup_counter[reqId] == 0 else f"-{dup_counter[reqId]}"
            dup_counter[reqId] += 1
            outputs.append({
                "type": "ud",
                "client": client,
                "id": f"{reqId}{suffix}",
                "dur": ud,
                "ts": rs,
                "details": {"req_send": rs, "req_recv": rr, "resp_send": rps, "resp_recv": rpr}
            })

        # Emit RDMA sequences
        for seq in rdma_seqs:
            rs = seq[0]["t"]   # rdma_send
            rr = seq[1]["t"]   # rdma_recv
            rd = rr - rs
            suffix = "" if dup_counter[reqId] == 0 else f"-{dup_counter[reqId]}"
            dup_counter[reqId] += 1
            outputs.append({
                "type": "rdma",
                "client": client,
                "id": f"{reqId}{suffix}",
                "dur": rd,
                "ts": rs,
                "details": {"rdma_send": rs, "rdma_recv": rr}
            })

    # sort all outputs chronologically by their sequence timestamp (ts)
    outputs.sort(key=lambda x: x["ts"])
    if debug:
        # show intermediate values and flag suspicious durations
        for o in outputs:
            if o["type"] == "ud":
                d = o["dur"]
                ds = o["details"]
                suspicious = d < 0 or d > 10_000_000_000  # arbitrary big threshold
                note = "  <-- suspicious" if suspicious else ""
                print(f"UD  id={o['id']} client={o['client']} dur={d} ts={o['ts']}{note}")
                print(f"     (resp_recv-req_send)={(ds['resp_recv']-ds['req_send'])} - (resp_send-req_recv)={(ds['resp_send']-ds['req_recv'])}")
            else:
                d = o["dur"]
                ds = o["details"]
                suspicious = d < 0 or d > 10_000_000_000
                note = "  <-- suspicious" if suspicious else ""
                print(f"RDMA id={o['id']} client={o['client']} dur={d} ts={o['ts']}{note}")
                print(f"     (rdma_recv - rdma_send) = ({ds['rdma_recv']} - {ds['rdma_send']})")
            print()
    return outputs

def write_outputs(outputs, out_path):
    with open(out_path, "w") as f:
        for o in outputs:
            f.write(f"[{o['type']}] client={o['client']} id={o['id']} dur_ns={o['dur']}\n")


def main():
    # Default behavior: process each sweeps/<tag>/grouped_flows.txt into sweeps/<tag>/ns3_output.txt
    import pathlib

    sweeps_dir = pathlib.Path("sweeps")
    if not sweeps_dir.exists() or not sweeps_dir.is_dir():
        print(f"[error] sweeps path not found: {sweeps_dir}", file=sys.stderr)
        sys.exit(1)

    wrote = 0
    for sub in sorted(sweeps_dir.iterdir()):
        if not sub.is_dir():
            continue
        grouped = sub / "grouped_flows.txt"
        if not grouped.exists():
            continue
        try:
            flows = parse_file(str(grouped))
            outputs = compute(flows, debug=False)
            write_outputs(outputs, sub / "ns3_output.txt")
            wrote += 1
        except Exception as e:
            print(f"[warn] failed to process {grouped}: {e}", file=sys.stderr)
            continue

    print(f"[done] Wrote ns3_output.txt to {wrote} subdirectories under {sweeps_dir}")

if __name__ == "__main__":
    main()

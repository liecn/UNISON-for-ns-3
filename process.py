from collections import OrderedDict
import re
import pathlib
import sys


def process_stream_into_groups(stream, groups):
    for raw in stream:
        line = raw.strip()
        if not line:
            continue
        match = re.search(r"\breqId=(\d+)", line)
        if not match:
            continue
        reqid = match.group(1)
        if reqid not in groups:
            groups[reqid] = []
        groups[reqid].append(line)


def write_groups(groups, out_path: pathlib.Path):
    with out_path.open("w") as f:
        for reqid, lines in groups.items():
            f.write(f"### reqId={reqid} ###\n")
            for l in lines:
                f.write(l + "\n")
            f.write("\n")


def main():
    # Default behavior: process each sweeps/<tag>/stdout.txt into sweeps/<tag>/grouped_flows.txt
    sweeps_dir = pathlib.Path("sweeps")
    if not sweeps_dir.exists() or not sweeps_dir.is_dir():
        print(f"[error] sweeps path not found: {sweeps_dir}", file=sys.stderr)
        sys.exit(1)

    wrote = 0
    for sub in sorted(sweeps_dir.iterdir()):
        if not sub.is_dir():
            continue
        stdout_file = sub / "stdout.txt"
        if not stdout_file.exists():
            continue
        local_groups = OrderedDict()
        try:
            with stdout_file.open("r") as f:
                process_stream_into_groups(f, local_groups)
        except Exception as e:
            print(f"[warn] failed to read {stdout_file}: {e}", file=sys.stderr)
            continue
        write_groups(local_groups, sub / "grouped_flows.txt")
        wrote += 1

    print(f"[done] Wrote grouped_flows.txt to {wrote} subdirectories under {sweeps_dir}")


if __name__ == "__main__":
    main()

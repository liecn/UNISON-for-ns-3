#!/usr/bin/env python3
import subprocess
import shutil
import pathlib
import time
import datetime as dt
import os
import sys
import argparse
import concurrent.futures as cf
from typing import List, Dict


WINDOW_SIZES: List[int] = [2, 4, 8, 12, 16]
RDMA_SIZES: List[int] = [
    102408, 204808, 256008, 307208, 409608,
    512008, 665608, 768008, 921608, 1024008, 1048584,
]
# Base mapping of RDMA size to short title for directory names
RDMA_TITLES_BASE: Dict[int, str] = {
    102408: "100",
    204808: "200",
    256008: "250",
    307208: "300",
    409608: "400",
    512008: "500",
    665608: "650",
    768008: "750",
    921608: "900",
    1024008: "1000",
    1048584: "10000",
}


def run_cmd(cmd: List[str], cwd: pathlib.Path, stdout_path: pathlib.Path, stderr_path: pathlib.Path) -> int:
    with stdout_path.open("w") as out, stderr_path.open("w") as err:
        proc = subprocess.run(cmd, cwd=str(cwd), stdout=out, stderr=err, text=True)
        return proc.returncode


def ensure_built(ns3_root: pathlib.Path) -> pathlib.Path:
    """Build the ns-3 scratch testbed target and return the resolved binary path."""
    build_dir = ns3_root / "cmake-cache"
    build_dir.mkdir(parents=True, exist_ok=True)
    print("[build] Building ns-3 scratch_testbed via CMake...")
    stdout = build_dir / "build_stdout.txt"
    stderr = build_dir / "build_stderr.txt"
    rc = run_cmd(["cmake", "--build", ".", "--target", "scratch_testbed", f"-j{os.cpu_count() or 1}"], cwd=build_dir, stdout_path=stdout, stderr_path=stderr)
    if rc != 0:
        print(f"[build] CMake build failed with code {rc}. See {stderr}")
        sys.exit(rc)
    # Resolve the produced binary path (ns3.<ver>-testbed-optimized)
    bin_dir = ns3_root / "build" / "scratch"
    candidates = sorted(bin_dir.glob("ns3.*-testbed-optimized"))
    if not candidates:
        print(f"[build] Could not find testbed binary under {bin_dir}")
        sys.exit(1)
    binary = candidates[0]
    print(f"[build] Using binary: {binary}")
    return binary


def clean_logs(_root: pathlib.Path) -> None:
    # No-op for ns-3 testbed (we capture stdout/stderr per run)
    return


def collect_outputs(_root: pathlib.Path, _dest_dir: pathlib.Path) -> None:
    # No additional outputs besides stdout/stderr for this testbed
    return


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Sweep maxWindows and dataBytes for ns-3 testbed")
    parser.add_argument("--ws", type=str, default="", help="Comma-separated window sizes (override defaults)")
    parser.add_argument("--rdma", type=str, default="", help="Comma-separated RDMA sizes (override defaults)")
    parser.add_argument("--jobs", type=int, default=(os.cpu_count() or 1), help="Max concurrent runs")
    return parser.parse_args()


def main() -> None:
    args = parse_args()

    # Resolve project paths
    ns3_root = pathlib.Path("/home/ubuntu/m4/High-Precision-Congestion-Control/UNISON-for-ns-3")
    results_root = ns3_root / "sweeps"
    if results_root.exists():
        shutil.rmtree(results_root)
    results_root.mkdir(parents=True, exist_ok=True)

    # Optional overrides via CLI
    ws_list = WINDOW_SIZES
    rdma_list = RDMA_SIZES
    if args.ws:
        ws_list = [int(x) for x in args.ws.split(",") if x.strip()]
    if args.rdma:
        rdma_list = [int(x) for x in args.rdma.split(",") if x.strip()]

    # Build mapping for chosen RDMA sizes with sensible defaults
    rdma_titles: Dict[int, str] = {r: RDMA_TITLES_BASE.get(r, str(r)) for r in rdma_list}

    # Build and locate testbed binary
    binary = ensure_built(ns3_root)

    total = len(ws_list) * len(rdma_list)
    print(f"[sweep] Scheduling {total} runs with up to {args.jobs} concurrent jobs...")

    def do_one(ws_val: int, rdma_val: int, idx: int) -> str:
        run_tag_local = f"{rdma_titles[rdma_val]}_{ws_val}"
        run_dir_local = results_root / run_tag_local
        run_dir_local.mkdir(parents=True, exist_ok=True)
        stdout_path = run_dir_local / "stdout.txt"
        stderr_path = run_dir_local / "stderr.txt"
        rc_local = run_cmd(
            [str(binary), f"--maxWindows={ws_val}", f"--dataBytes={rdma_val}"],
            cwd=ns3_root,
            stdout_path=stdout_path,
            stderr_path=stderr_path,
        )
        collect_outputs(ns3_root, run_dir_local)
        status = "ok" if rc_local == 0 else f"fail({rc_local})"
        return f"[{idx}/{total}] {run_tag_local}: {status}"

    futures = []
    idx = 0
    with cf.ThreadPoolExecutor(max_workers=args.jobs) as ex:
        for ws in ws_list:
            for rdma in rdma_list:
                idx += 1
                futures.append(ex.submit(do_one, ws, rdma, idx))
        for fut in cf.as_completed(futures):
            try:
                print(fut.result(), flush=True)
            except Exception as e:
                print(f"[error] {e}", flush=True)

    print(f"[done] Results under {results_root}")


if __name__ == "__main__":
    main()



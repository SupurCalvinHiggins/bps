#!/usr/bin/env python3
import sys
import argparse
import math
import os
import re
import sys
# *************************************************************
# Benchmark Sets (inlined from bench_list.pl)
# *************************************************************

SUITES = {
    "SHORT": """SHORT-FP-1
SHORT-FP-2
SHORT-FP-3
SHORT-FP-4
SHORT-FP-5
SHORT-INT-1
SHORT-INT-2
SHORT-INT-3
SHORT-INT-4
SHORT-INT-5
SHORT-MM-1
SHORT-MM-2
SHORT-MM-3
SHORT-MM-4
SHORT-MM-5
SHORT-SERV-1
SHORT-SERV-2
SHORT-SERV-3
SHORT-SERV-4
SHORT-SERV-5""",
    "LONG": """LONG-SPEC2K6-00
LONG-SPEC2K6-01
LONG-SPEC2K6-02
LONG-SPEC2K6-03
LONG-SPEC2K6-04
LONG-SPEC2K6-05
LONG-SPEC2K6-06
LONG-SPEC2K6-07
LONG-SPEC2K6-08
LONG-SPEC2K6-09
LONG-SPEC2K6-10
LONG-SPEC2K6-11
LONG-SPEC2K6-12
LONG-SPEC2K6-13
LONG-SPEC2K6-14
LONG-SPEC2K6-15
LONG-SPEC2K6-16
LONG-SPEC2K6-17
LONG-SPEC2K6-18
LONG-SPEC2K6-19""",
}

# "all" is just SHORT + LONG
SUITES["all"] = SUITES["SHORT"] + "\n" + SUITES["LONG"]

# =============================================================
# Defaults
# =============================================================
stat = "MISPRED_PER_1K_INST"
wsuite = "all"
amean = True
gmean = True
debug = False
noxxxx = False
topk = None  # limit directories, not workloads

dirs = []
data = []
w = []
num_w = 0


# =============================================================
def usage():
    print("Usage: script.py <-options> -d <dir1> ... <dirN>", file=sys.stderr)
    print("\t-h                     : help -- print this menu.")
    print("\t-d <statdirs>          : directory for stats (multiple ok, if -d is last)")
    print("\t-s <statname>          : name of the stat (wildcard ok)")
    print("\t-w <workload/suite>    : name of the workload suite from SUITES")
    print("\t-noxxxx                : Print 0 for no data instead of xxxx.")
    print("\t-topk N                : Print only the top N directories (lowest AMEAN).")
    sys.exit(1)


# =============================================================
def init_stats():
    global data
    data = [[0 for _ in range(num_w)] for _ in range(len(dirs))]


# =============================================================
def get_stats():
    global data
    for dirnum, d in enumerate(dirs):
        for ii, wname in enumerate(w):
            fname = os.path.join(d, f"{wname}.res")
            try:
                with open(fname, "r") as f:
                    lines = f.readlines()
            except IOError:
                print(f"cannot open {fname} for read")
                lines = []

            for line in lines:
                words = line.split()
                for jj in range(len(words) - 1):
                    if re.search(stat, words[jj]):
                        pos = jj + 2
                        if pos < len(words):
                            try:
                                val = float(words[pos])
                                data[dirnum][ii] += val
                                if debug:
                                    print(
                                        f"stat match for {stat} found in line: {line.strip()}"
                                    )
                            except ValueError:
                                pass


# =============================================================
def select_topk_dirs():
    """Return filtered dirs + data if topk is set."""
    if topk is None or topk >= len(dirs):
        return dirs, data

    amean_per_dir = []
    for dnum, d in enumerate(dirs):
        vals = [data[dnum][ii] for ii in range(num_w)]
        mean = sum(vals) / num_w if num_w else float("inf")
        amean_per_dir.append((dnum, mean))

    amean_per_dir.sort(key=lambda x: x[1])  # lowest is best
    keep_indices = [dnum for dnum, _ in amean_per_dir[:topk]]

    new_dirs = [dirs[i] for i in keep_indices]
    new_data = [data[i] for i in keep_indices]
    return new_dirs, new_data


# =============================================================
def print_header(cur_dirs):
    header = ""
    for d in cur_dirs:
        d = d.rstrip("/")
        mystring = d
        header += mystring + "\t"
    print(f"\n{'ResultDirs ==>':<20}\t{header}")


# =============================================================
def print_stats(cur_dirs, cur_data):
    for ii, wname in enumerate(w):
        print(f"\n{wname}\t", end="")
        for dirnum in range(len(cur_dirs)):
            val = cur_data[dirnum][ii]
            print_val(val)
    print("\n")


# =============================================================
def print_val(val):
    if val:
        print(f"{val:12.3f}\t", end="")
    else:
        if not noxxxx:
            print("xxxxxxxxxxx \t", end="")
        else:
            print("0           \t", end="")


# =============================================================
def print_amean(cur_dirs, cur_data):
    print(f"\nAMEAN\t", end="")
    for dirnum in range(len(cur_dirs)):
        s = sum(cur_data[dirnum][ii] for ii in range(num_w))
        val = s / num_w if num_w else 0
        print_val(val)
    print("\n")


# =============================================================
def print_gmean(cur_dirs, cur_data):
    print(f"\nGMEAN\t", end="")
    for dirnum in range(len(cur_dirs)):
        vals = [cur_data[dirnum][ii] for ii in range(num_w) if cur_data[dirnum][ii] > 0]
        if len(vals) < num_w:  # missing data => gmean=0
            val = 0
        else:
            logs = [math.log(v) for v in vals]
            val = math.exp(sum(logs) / num_w)
        print_val(val)
    print("\n\n")


# =============================================================
def main():
    global stat, wsuite, debug, noxxxx, w, num_w, topk

    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("-h", action="store_true")
    parser.add_argument("-d", nargs="+")
    parser.add_argument("-s")
    parser.add_argument("-w")
    parser.add_argument("-debug", action="store_true")
    parser.add_argument("-noxxxx", action="store_true")
    parser.add_argument("-topk", type=int)
    args, unknown = parser.parse_known_args()

    if args.h:
        usage()

    if args.d:
        for d in args.d:
            dirs.append(d if d.endswith("/") else d + "/")

    if args.s:
        stat = args.s

    if args.w:
        wsuite = args.w

    debug = args.debug
    noxxxx = args.noxxxx
    topk = args.topk

    if wsuite not in SUITES:
        sys.exit(f"No benchmark set '{wsuite}' defined in SUITES")

    w = SUITES[wsuite].split()
    num_w = len(w)

    init_stats()
    get_stats()

    # filter dirs if topk set
    cur_dirs, cur_data = select_topk_dirs()

    print_header(cur_dirs)
    print_stats(cur_dirs, cur_data)
    if amean:
        print_amean(cur_dirs, cur_data)
    if gmean:
        print_gmean(cur_dirs, cur_data)


# =============================================================
if __name__ == "__main__":
    main()

import argparse
import itertools as it
import subprocess
import time
from pathlib import Path

PARAMS = {
    "GAG": {"GHR_SIZE": list(range(1, 20)), "CNT_SIZE": [1, 2, 3]},
    "PAG": {
        "LHR_BITS": [1, 10],  # list(range(1, 20)),
        "LHT_BITS": [10],  # list(range(1, 20)),
        "CNT_BITS": [2],
        "USE_HASH": [0],
    },
    "TBP": {
        "USE_GSHARE": [0, 1],
        "LHT_BITS": [10, 12, 14],
        "LPT_BITS": [10, 12, 14],
        "GPT_BITS": [17, 18],
        "GHR_BITS": [17, 18],
        "BPT_BITS": [10, 12, 14],
        "LHT_R_BITS": [10, 12, 14],
        "LPT_R_BITS": [2, 3],
        "GPT_R_BITS": [2, 3],
        "BPT_R_BITS": [2, 3],
    },
    "HP": {
        "THRESHOLD": [-30, -1, 0, 1],
        "PWT_BITS": [11, 12],
        "GHT_LEN": [7, 15],
        "GHL_GEO_R": [1.2, 1.3, 1.4],
        "GHL_GEO_A": [3, 4],
    },
    "CUSTOM": {
        "HISTORY_COUNT": [7, 12, 15, 18, 24],
        "HISTORY_GEO_START": [3, 4],
        "HISTORY_GEO_FACTOR": [1.2, 1.3, 1.4],
        "WEIGHT_TABLE_LEN": [2048, 4096, 8192],
        "THRESHOLD": [0, 1, 15],
        "UPDATE_THRESHOLD_INIT": [10],
        "UPDATE_THRESHOLD_SPEED": [16, 18, 20],
        "PC_HISTORY_THRESHOLD": [9, 17, 23],
    },
}
SIZE = {
    "GAG": lambda c: c["GHR_SIZE"] + c["CNT_SIZE"] * 2 ** c["GHR_SIZE"],
    "PAG": lambda c: c["LHT_BITS"] * 2 ** c["LHR_BITS"]
    + c["CNT_BITS"] * 2 ** c["LHT_BITS"],
    "TBP": lambda c: c["LHT_R_BITS"] * 2 ** c["LHT_BITS"]
    + c["LPT_R_BITS"] * 2 ** c["LPT_BITS"]
    + c["GPT_R_BITS"] * 2 ** c["GPT_BITS"]
    + c["GHR_BITS"]
    + c["BPT_R_BITS"] * 2 ** c["BPT_BITS"],
    "HP": lambda c: c["GHT_LEN"] * 8 * 2 ** c["PWT_BITS"]
    + int(c["GHL_GEO_A"] * c["GHL_GEO_R"] ** (c["GHT_LEN"] - 1))
    + 1,
    "CUSTOM": lambda c: 8 * c["HISTORY_COUNT"] * c["WEIGHT_TABLE_LEN"]
    + c["HISTORY_GEO_START"] * c["HISTORY_GEO_FACTOR"] ** (c["HISTORY_COUNT"] - 2)
    + 1
    + 64,
}

MAX_SIZE = 2**19
MIN_SIZE = (1 / 1.1) * MAX_SIZE

BUILDS_PATH = Path("./builds")
SCRIPTS_PATH = Path("./scripts")
RESULTS_PATH = Path("./results")


def is_frontier(predictor: str, comb: dict):
    size = SIZE[predictor](comb)
    return size <= MAX_SIZE and size >= MIN_SIZE


def uuid(predictor: str, comb: dict) -> str:
    kv = sorted(comb.items())
    return predictor + "-" + "-".join(f"{k}.{v}" for k, v in kv)


def run(cmd: str, cwd=None) -> None:
    print(cmd)
    subprocess.run(cmd, shell=True, cwd=cwd)


def build(predictor: str, comb: dict):
    name = uuid(predictor, comb)

    build_path = BUILDS_PATH / name
    if build_path.exists():
        return

    predictor_flags = " ".join(f"-D{k}={v}" for k, v in comb.items())
    cmd = f'cmake -S . -B build -DPREDICTOR={predictor} -DPREDICTOR_FLAGS="{predictor_flags}"'
    run(cmd)

    cmd = "cmake --build build"
    run(cmd)

    cmd = f"mv build/bps {build_path}"
    run(cmd)


def bench(predictor: str, comb: dict):
    name = uuid(predictor, comb)

    build_path = BUILDS_PATH / name
    if not build_path.exists():
        return

    result_path = RESULTS_PATH / name
    if result_path.exists():
        return

    cmd = f"./runall.pl -s {build_path.absolute()} -w all -f 8 -d {result_path.absolute()}"
    run(cmd, cwd="./scripts")


def wait():
    while True:
        out = subprocess.run(
            ["pgrep", "-f", "perl|sim.bin"], capture_output=True, text=True
        )
        if out.returncode != 0:
            break
        time.sleep(1)


def display(predictor: str):
    cmd = f"./getdata.pl -d ../results/{predictor}*"
    run(cmd, cwd=SCRIPTS_PATH)


def get_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser()
    parser.add_argument("predictor")
    parser.add_argument("--clean", action="store_true")
    return parser


def main() -> None:
    parser = get_parser()
    args = parser.parse_args()

    predictor = args.predictor.upper()

    if args.clean:
        for path in it.chain(
            BUILDS_PATH.glob(f"{predictor}-*"),
            RESULTS_PATH.glob(f"{predictor}-*/*.res"),
            RESULTS_PATH.glob(f"{predictor}-*/*.bin"),
        ):
            path.unlink()

        for path in RESULTS_PATH.glob(f"{predictor}-*/"):
            path.rmdir()

    params = PARAMS[predictor]
    combs = [dict(zip(params, v)) for v in it.product(*params.values())]
    combs = [c for c in combs if is_frontier(predictor, c)]
    print(f"{len(combs)} combinations")

    for comb in combs:
        build(predictor, comb)

    for comb in combs:
        bench(predictor, comb)

    # The perl scripts are interesting... We need to wait, or some background tasks do
    # not finish in time.
    wait()

    display(predictor)


if __name__ == "__main__":
    main()

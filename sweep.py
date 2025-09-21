import itertools as it
import subprocess
from pathlib import Path

params = {"GHT_SIZE": [17, 18, 19], "CNT_SIZE": [1, 2, 3]}

combs = [dict(zip(params, v)) for v in it.product(*params.values())]


build_path = Path("./build")
result_path = Path("./build")


def get_id(comb: dict) -> str:
    kv = sorted(comb.items())
    return "-".join(f"{k}.{v}" for k, v in kv)


for comb in combs:
    name = f"GAG-{get_id(comb)}"
    path = build_path / name
    # if path.exists():
    #    continue
    flags = " ".join(f"-D{k}={v}" for k, v in comb.items())
    cmd = [
        "g++",
        "./sim/main.cc",
        "./sim/predictor.cc",
        "./sim/tracer.cc",
        "-g",
        "-O3",
        "-Wall",
        "-Isim",
        "-DUSE_GAG",
        flags,
        "-o",
        path.absolute().as_posix(),
    ]
    subprocess.run(cmd)


for path in build_path.glob("*"):
    name = path.name
    # if (result_path / name).exists():
    #   continue
    subprocess.run(
        [
            "./runall.pl",
            "-s",
            path.absolute().as_posix(),
            "-w",
            "all",
            "-f",
            "8",
            "-d",
            f"../results/{name}",
        ],
        cwd="./scripts",
    )

for path in build_path.glob("*"):
    name = path.name
    subprocess.run(
        [
            "./getdata.pl",
            "-d",
            f"../results/{name}",
        ],
        cwd="./scripts",
    )

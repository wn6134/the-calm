"""Render a capture manifest through the offline renderer (build/calm_render) with several seeds.

    python render_matrix.py <manifest.json> <signals_dir> <out_dir> [seeds=1,2,3] [extra key=value ...]

Writes <case>_s<seed>.wav for every case. Much faster than going through a plugin host.
"""
import json
import pathlib
import subprocess
import sys


def main():
    manifest = json.loads(pathlib.Path(sys.argv[1]).read_text())
    signals, out = pathlib.Path(sys.argv[2]), pathlib.Path(sys.argv[3])
    out.mkdir(parents=True, exist_ok=True)
    seeds, extra = [1], []
    for arg in sys.argv[4:]:
        if arg.startswith("seeds="):
            seeds = [int(s) for s in arg[6:].split(",")]
        else:
            extra.append(arg)
    procs = []
    for c in manifest["cases"]:
        args = [f"{k}={v}" for k, v in c["knobs"].items()] + ["rise=0.69"]
        for sw in ("oct1", "oct2"):
            if c[sw]:
                args.append(f"{sw}={c[sw][0]}:{c[sw][1]}")
        for seed in seeds:
            cmd = ["build/calm_render", str(signals / f"{c['signal']}.wav"), str(out / f"{c['name']}_s{seed}.wav"),
                   *args, *extra, f"seed={seed}"]
            procs.append(subprocess.Popen(cmd))
            if len(procs) >= 16:
                procs.pop(0).wait()
    for p in procs:
        p.wait()


if __name__ == "__main__":
    main()

"""Render a fixed test matrix through a plugin (the reference plugin or The Calm).

    python capture.py list   <plugin>
    python capture.py render <plugin> <signals_dir> <out_dir> <param_map.json> [filter|-] [main|probe|probe2]

`list` prints every parameter with its display text across the 0..1 range, which tells us
how the knobs map to real units. `render` writes one WAV per case plus manifest.json.

param_map.json maps logical names (panic, chaos, speed, rise, octave1, octave2, gate, blend,
input, output, stereo) to the plugin's own parameter names.
"""
import json
import pathlib
import sys

import numpy as np
import pedalboard
import soundfile as sf

SR = 48000
CHUNK = 128  # parameter changes land on chunk boundaries (~2.7 ms)

PRESS, RELEASE = 0.5, 3.0


def case(name, signal, knobs=None, oct1=None, oct2=None, extra=None):
    return {"name": name, "signal": signal, "knobs": knobs or {}, "oct1": oct1, "oct2": oct2, "extra": extra or {}}


def matrix():
    cases = []
    steps = [0.0, 0.25, 0.5, 0.75, 1.0]
    hold = (PRESS, RELEASE)
    for sig in ["sine220", "sine82", "pluckA3", "pluckE2", "power", "sweep", "noise"]:
        cases.append(case(f"bypass_{sig}", sig))
        cases.append(case(f"oct1_{sig}", sig, oct1=hold))
        cases.append(case(f"oct2_{sig}", sig, oct2=hold))
    cases.append(case("both_sine220", "sine220", oct1=hold, oct2=hold))
    cases.append(case("oct1_then_oct2_sine220", "sine220", oct1=(0.5, 3.0), oct2=(1.5, 2.5)))
    for r in [0.0, 0.1, 0.2, 0.35, 0.5, 0.65, 0.8, 1.0]:
        cases.append(case(f"rise{r:.2f}_oct1_sine220", "sine220", {"rise": r}, oct1=hold))
        cases.append(case(f"rise{r:.2f}_oct2_sine220", "sine220", {"rise": r}, oct2=hold))
    for knob in ["panic", "chaos", "speed"]:
        for v in steps[1:]:
            sig = "sine220_long" if knob in ("chaos", "speed") else "sine220"
            rel = (PRESS, 8.5) if sig == "sine220_long" else hold
            cases.append(case(f"{knob}{v:.2f}_oct1_{sig}", sig, {knob: v}, oct1=rel))
            cases.append(case(f"{knob}{v:.2f}_oct2_{sig}", sig, {knob: v}, oct2=rel))
            cases.append(case(f"{knob}{v:.2f}_bypass_sine220", "sine220", {knob: v}))
            cases.append(case(f"{knob}{v:.2f}_oct1_pluckE2", "pluckE2", {knob: v}, oct1=hold))
            cases.append(case(f"{knob}{v:.2f}_oct1_power", "power", {knob: v}, oct1=hold))
    for p, c, s in [(1, 1, 1), (0.5, 0.5, 0.5), (1, 0, 0.5), (0, 1, 1)]:
        tag = f"p{p}_c{c}_s{s}"
        cases.append(case(f"combo_{tag}_oct1_power", "power", {"panic": p, "chaos": c, "speed": s}, oct1=hold))
        cases.append(case(f"combo_{tag}_oct2_pluckE2", "pluckE2", {"panic": p, "chaos": c, "speed": s}, oct2=hold))
    for b in [0.0, 0.5]:
        cases.append(case(f"blend{b:.2f}_oct1_sine220", "sine220", {"blend": b}, oct1=hold))
    for g in [0.3, 0.6]:
        cases.append(case(f"gate{g:.2f}_oct1_quiet220", "quiet220", {"gate": g}, oct1=hold))
    cases.append(case("stereo_oct1_power", "power", {"stereo": 1.0, "chaos": 1.0}, oct1=hold, extra={"stereo_in": True}))
    return cases


def probe_matrix():
    cases = []
    hold = (PRESS, 5.5)
    for v in np.round(np.linspace(0, 1, 11), 2):
        cases.append(case(f"probe_panic{v:.2f}_oct1", "probe", {"panic": float(v)}, oct1=hold))
        cases.append(case(f"probe_panic{v:.2f}_oct2", "probe", {"panic": float(v)}, oct2=hold))
    for v in [0.5, 1.0]:
        cases.append(case(f"probe_chaos{v:.2f}_oct1", "probe", {"chaos": v}, oct1=hold))
        cases.append(case(f"probe_speed{v:.2f}_oct1", "probe", {"speed": v}, oct1=hold))
    return cases


def probe2_matrix():
    cases = []
    hold = (PRESS, 3.8)
    for v in [0.0, 0.25, 0.5, 0.75, 0.9, 1.0]:
        for sig in ["noise", "impulses"]:
            cases.append(case(f"p2_speed{v:.2f}_oct1_{sig}", sig, {"speed": v}, oct1=hold))
            cases.append(case(f"p2_speed{v:.2f}_oct2_{sig}", sig, {"speed": v}, oct2=hold))
        cases.append(case(f"p2_speed{v:.2f}_bypass_impulses", "impulses", {"speed": v}))
    for v in [0.1, 0.25, 0.5, 0.75, 1.0]:
        cases.append(case(f"p2_chaos{v:.2f}_oct1_probe", "probe", {"chaos": v}, oct1=(PRESS, 5.5)))
        cases.append(case(f"p2_chaos{v:.2f}_speed0.5_oct1_probe", "probe", {"chaos": v, "speed": 0.5}, oct1=(PRESS, 5.5)))
    for v in [0.25, 1.0]:
        cases.append(case(f"p2_panic{v:.2f}_speed0.5_oct1_probe", "probe", {"panic": v, "speed": 0.5}, oct1=(PRESS, 5.5)))
    return cases


def probe3_matrix():
    cases = []
    for sig in ["noise", "noise_m10", "noise_m20", "noise_m30", "sine220_loud", "sine220", "sine220_m20", "sine2k"]:
        for sw in ("oct1", "oct2"):
            cases.append(case(f"p3_{sw}_{sig}", sig, **{sw: (PRESS, 3.8)}))
    for sw in ("oct1", "oct2"):
        cases.append(case(f"p3_{sw}_hisweep", "hisweep", **{sw: (0.0, 9.0)}))
    cases.append(case("p3_bypass_hisweep", "hisweep"))
    return cases


def di_matrix():
    cases = []
    hold = (0.5, 19.5)
    pulses = [(0.5 + k * 1.5, 1.5 + k * 1.5) for k in range(12)]
    settings = [
        ("oct1", {}, "oct1"), ("oct2", {}, "oct2"),
        ("oct1_panic050", {"panic": 0.5}, "oct1"), ("oct1_chaos050", {"chaos": 0.5}, "oct1"),
        ("oct1_speed075", {"speed": 0.75}, "oct1"),
        ("oct1_default", {"panic": 0.5, "chaos": 0.5, "speed": 0.5}, "oct1"),
        ("oct2_default", {"panic": 0.5, "chaos": 0.5, "speed": 0.5}, "oct2"),
        ("oct1_chaos1_speed1", {"chaos": 1.0, "speed": 1.0}, "oct1"),
        ("oct1_max", {"panic": 1.0, "chaos": 1.0, "speed": 1.0}, "oct1"),
        ("oct2_max", {"panic": 1.0, "chaos": 1.0, "speed": 1.0}, "oct2"),
        ("oct1_rise065_pulses", {"rise": 0.65}, "pulses"),
    ]
    for sig in ["diA", "diB", "diC", "diD"]:
        for name, knobs, sw in settings:
            if sw == "pulses":
                cases.append(case(f"di_{sig}_{name}", sig, knobs, oct1=pulses))
            else:
                cases.append(case(f"di_{sig}_{name}", sig, knobs, **{sw: hold}))
    return cases


def combo_matrix():
    cases = []
    hold = (0.5, 7.5)
    settings = [
        ("c1", {"chaos": 1.0}), ("s1", {"speed": 1.0}), ("c05s1", {"chaos": 0.5, "speed": 1.0}),
        ("c1s1", {"chaos": 1.0, "speed": 1.0}), ("c1s075", {"chaos": 1.0, "speed": 0.75}),
        ("p1s1", {"panic": 1.0, "speed": 1.0}), ("p1c1", {"panic": 1.0, "chaos": 1.0}),
        ("max", {"panic": 1.0, "chaos": 1.0, "speed": 1.0}),
    ]
    for sig in ["noise8", "sine220_8"]:
        for name, knobs in settings:
            for sw in ("oct1", "oct2"):
                cases.append(case(f"cb_{sig}_{name}_{sw}", sig, knobs, **{sw: hold}))
    return cases


def lf_matrix():
    cases = []
    hold = (0.5, 7.5)
    settings = [("none", {}), ("s1", {"speed": 1.0}), ("c1", {"chaos": 1.0}), ("c05s1", {"chaos": 0.5, "speed": 1.0}),
                ("c1s1", {"chaos": 1.0, "speed": 1.0}), ("max", {"panic": 1.0, "chaos": 1.0, "speed": 1.0})]
    for sig in ["noise_hp200", "noise_lp150", "sine25", "sine40", "sine60"]:
        for name, knobs in settings:
            for sw in ("oct1", "oct2"):
                cases.append(case(f"lf_{sig}_{name}_{sw}", sig, knobs, **{sw: hold}))
    return cases


MATRICES = {"lf": lf_matrix, "combo": combo_matrix, "di": di_matrix, "main": matrix, "probe": probe_matrix, "probe2": probe2_matrix, "probe3": probe3_matrix}


def describe(plugin):
    out = {}
    for name, param in plugin.parameters.items():
        original = param.raw_value
        texts = []
        for v in np.linspace(0, 1, 21):
            param.raw_value = float(v)
            texts.append((round(float(v), 2), param.string_value))
        param.raw_value = original
        out[name] = {"default_raw": original, "range": [param.min_value, param.max_value], "texts": texts}
    return out


def set_raw(plugin, name, value):
    plugin.parameters[name].raw_value = float(value)


def render_case(plugin, pmap, c, signals_dir):
    audio, sr = sf.read(signals_dir / f"{c['signal']}.wav", dtype="float32")
    assert sr == SR
    if audio.ndim == 1:
        audio = np.stack([audio, audio])
        if c["extra"].get("stereo_in"):
            audio[1] = np.roll(audio[1], 480) * 0.8
    else:
        audio = audio.T

    # Neutral baseline: gate open, full wet, all knobs down, switches up, mono.
    # Rise 0.1 normalised is ~0.7 ms on both plugins (0..1500 ms, centre 150 ms).
    base = {"gate": 0.0, "blend": 1.0, "panic": 0.0, "chaos": 0.0, "speed": 0.0, "stereo": 0.0,
            "octave1": 0.0, "octave2": 0.0, "rise": 0.1}
    base.update(c["knobs"])
    for key, value in base.items():
        if key in pmap:
            set_raw(plugin, pmap[key], value)

    plugin.reset()
    n = audio.shape[1]
    # Pad with silence so a latency-compensating host can flush the tail.
    padded = np.concatenate([audio, np.zeros((audio.shape[0], SR // 2), dtype=audio.dtype)], axis=1)
    pieces, produced = [], 0
    state = {}
    for start in range(0, padded.shape[1], CHUNK):
        t = start / SR
        for sw in ("oct1", "oct2"):
            win = c[sw]
            # A window is (on, off) or a list of such pairs.
            wins = [] if win is None else ([win] if isinstance(win[0], (int, float)) else win)
            on = any(a <= t < b for a, b in wins)
            if state.get(sw) != on:  # setting parameters is slow in some hosts; only on change
                set_raw(plugin, pmap["octave1" if sw == "oct1" else "octave2"], 1.0 if on else 0.0)
                state[sw] = on
        # pedalboard trims reported latency, so chunks may come back shorter at first.
        block = plugin.process(padded[:, start:start + CHUNK], SR, reset=False)
        pieces.append(block)
        produced += block.shape[1]
        if produced >= n:
            break
    return np.concatenate(pieces, axis=1)[:, :n]


def main():
    mode, plugin_path = sys.argv[1], sys.argv[2]
    plugin = pedalboard.load_plugin(plugin_path)

    if mode == "list":
        print(json.dumps(describe(plugin), indent=1, ensure_ascii=False))
        return

    signals_dir, out_dir = pathlib.Path(sys.argv[3]), pathlib.Path(sys.argv[4])
    pmap = json.loads(pathlib.Path(sys.argv[5]).read_text())
    only = sys.argv[6] if len(sys.argv) > 6 and sys.argv[6] != "-" else None
    which = sys.argv[7] if len(sys.argv) > 7 else "main"
    out_dir.mkdir(parents=True, exist_ok=True)

    manifest = {"plugin": plugin_path, "param_map": pmap, "latency": getattr(plugin, "latency_samples", None),
                "parameters": describe(plugin), "cases": []}
    for c in MATRICES[which]():
        if only and only not in c["name"]:
            continue
        if "--skip-existing" in sys.argv and (out_dir / f"{c['name']}.wav").exists():
            manifest["cases"].append(c)
            continue
        out = render_case(plugin, pmap, c, signals_dir)
        sf.write(out_dir / f"{c['name']}.wav", out.T, SR, subtype="FLOAT")
        manifest["cases"].append(c)
        print(c["name"], flush=True)
    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=1, ensure_ascii=False))


if __name__ == "__main__":
    main()

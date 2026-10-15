"""Compare a reference plugin and The Calm across sample rates on the model's key measurements.

    python sr_check.py <reference.vst3> <reference_param_map.json> [rates=44100,48000,96000]

Renders a handful of probes through both plugins at each rate (signals are resampled from the
48 kHz set in reference/signals) and prints, per rate and plugin:
  shift      main spectral line for +1 octave on a 220 Hz sine, and the grain line spacing
  rise       time to 90 % of +1 octave with Rise at 150 ms
  chill      detuned voice offsets (cents) with Chill at 100 %
  sloth      first comb resonance on noise with Sloth at 100 % (+1 octave)
  alias      input frequency where +1 octave output starts to fall off (anti-alias cutoff)
  order      pitch spread (cents) and level dips with Order at 100 %
"""
import pathlib
import sys

import numpy as np
import soundfile as sf
from scipy import signal as sps

sys.path.insert(0, str(pathlib.Path(__file__).parent))
import analyze as A  # noqa: E402
import capture as C  # noqa: E402
import pedalboard  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parent.parent
PLUGINS = {
    "ref": (sys.argv[1] if len(sys.argv) > 2 else "", pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else None),
    "calm": (str(pathlib.Path.home() / "Library/Audio/Plug-Ins/VST3/The Calm.vst3"), ROOT / "reference/calm_map.json"),
}
SIGNALS = ["sine220", "sine220_long", "noise", "hisweep"]


def resampled_signals(rate):
    out = ROOT / f"reference/signals_{rate}"
    out.mkdir(exist_ok=True)
    for name in SIGNALS:
        x, sr = sf.read(ROOT / f"reference/signals/{name}.wav", dtype="float64")
        y = x if rate == sr else sps.resample_poly(x, rate, sr)
        sf.write(out / f"{name}.wav", y.astype(np.float32), rate, subtype="FLOAT")
    return out


def cases():
    hold = (0.5, 3.0)
    return [
        C.case("shift", "sine220", oct1=hold),
        C.case("rise", "sine220", {"rise": 0.5}, oct1=hold),  # 0.5 normalised = 150 ms on both
        C.case("chill", "sine220", {"panic": 1.0}, oct1=hold),
        C.case("sloth", "noise", {"speed": 1.0}, oct1=(0.5, 3.8)),
        C.case("sloth0", "noise", {}, oct1=(0.5, 3.8)),
        C.case("alias", "hisweep", oct1=(0.0, 9.0)),
        C.case("order", "sine220_long", {"chaos": 1.0}, oct1=(0.5, 8.5)),
    ]


def measure(results, rate):
    sr = rate
    A.SR = rate
    m = {}
    x = results["shift"]
    seg = x[int(1.2 * sr):int(2.9 * sr)] * np.hanning(int(2.9 * sr) - int(1.2 * sr))
    mag = np.abs(np.fft.rfft(seg, 1 << 19))
    f = np.fft.rfftfreq(1 << 19, 1 / sr)
    band = (f > 300) & (f < 600)
    db = 20 * np.log10(mag[band] / mag[band].max() + 1e-12)
    peaks, _ = sps.find_peaks(db, height=-45, distance=int(10 / f[1]))
    lines = np.sort(f[band][peaks])
    main = f[band][np.argmax(db)]
    spacing = np.median(np.diff(lines)) if len(lines) > 1 else float("nan")
    m["shift"] = f"main {main:6.1f} Hz, grain lines every {spacing:5.1f} Hz"

    t, f0, _ = A.yin(results["rise"])
    cents = 1200 * np.log2(f0 / 220.0)
    after = t >= 0.5
    reached = np.where(after & (np.nan_to_num(cents, nan=-1e9) >= 1080))[0]
    m["rise"] = f"{(t[reached[0]] - 0.5) * 1000:5.0f} ms to 90 %" if len(reached) else "-"

    x = results["chill"]
    seg = x[int(1.2 * sr):int(2.9 * sr)] * np.hanning(int(2.9 * sr) - int(1.2 * sr))
    mag = np.abs(np.fft.rfft(seg, 1 << 19))
    db = 20 * np.log10(mag / mag.max() + 1e-12)
    idx, _ = sps.find_peaks(db, height=-6, distance=int(15 / f[1]))
    top = sorted(f[idx][np.argsort(db[idx])[::-1][:3]])
    m["chill"] = "voices at " + ", ".join(f"{v:5.0f}" for v in top) + " Hz"

    def psd(y):
        ff, p = sps.welch(y[int(1.0 * sr):int(3.7 * sr)], sr, nperseg=1 << 14)
        return ff, 10 * np.log10(p + 1e-20)
    ff, p1 = psd(results["sloth"])
    _, p0 = psd(results["sloth0"])
    gain = p1 - p0
    lo = (ff > 100) & (ff < 260)
    m["sloth"] = f"first resonance {ff[lo][np.argmax(gain[lo])]:5.1f} Hz (+{gain[lo].max():4.1f} dB)"

    y = results["alias"]
    fs, ts, S = sps.spectrogram(y, sr, nperseg=4096, noverlap=3072)
    fin = 500 * (23000 / 500) ** (ts / 8.0)
    total = 10 * np.log10(S.sum(axis=0) + 1e-20)
    ref = np.median(total[(fin > 2000) & (fin < 6000)])
    drop = np.where((fin > 6000) & (total < ref - 20))[0]
    m["alias"] = f"output -20 dB from input {fin[drop[0]] / 1000:5.1f} kHz" if len(drop) else "-"

    x = results["order"]
    t, f0, _ = A.yin(x)
    mask = (t > 1.0) & (t < 8.3)
    c = 1200 * np.log2(f0[mask] / 220.0)
    c = c[~np.isnan(c)]
    env = A.envelope(x[int(1.0 * sr):int(8.3 * sr)], 2)
    m["order"] = f"pitch spread {np.std(c):4.0f} cents, level dips {env.std() / env.mean():4.2f}"
    A.SR = 48000
    return m


def main():
    rates = [44100, 48000, 96000]
    if PLUGINS["ref"][1] is None:
        sys.exit(__doc__)
    for arg in sys.argv[3:]:
        if arg.startswith("rates="):
            rates = [int(r) for r in arg[6:].split(",")]
    table = {}
    for rate in rates:
        sig_dir = resampled_signals(rate)
        C.SR = rate
        for name, (path, pmap_path) in PLUGINS.items():
            import json
            plugin = pedalboard.load_plugin(path)
            pmap = json.loads(pmap_path.read_text())
            results = {}
            for c in cases():
                out = C.render_case(plugin, pmap, c, sig_dir)
                results[c["name"]] = out.mean(axis=0).astype(np.float64)
            table[(rate, name)] = measure(results, rate)
            print(f"{rate} {name} done", flush=True)
    C.SR = 48000
    for key in ["shift", "rise", "chill", "sloth", "alias", "order"]:
        print(f"\n{key}")
        for rate in rates:
            for name in PLUGINS:
                print(f"  {rate:6d} {name:5s}  {table[(rate, name)][key]}")


if __name__ == "__main__":
    main()

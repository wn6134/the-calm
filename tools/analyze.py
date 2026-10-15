"""Measure and compare captures.

    python analyze.py features <dir>                 -> <dir>/features.json
    python analyze.py compare  <ref_dir> <test_dir> <out_dir> [filter]

Features per file: pitch track (YIN), rise time, pitch spread in the hold region,
amplitude-modulation rate/depth, dominant spectral peaks, band spectrum.
`compare` writes side-by-side plots and a summary table (report.md).
"""
import json
import pathlib
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import soundfile as sf
from scipy import signal as sps

SR = 48000
HOP = 240  # 5 ms
PRESS, RELEASE = 0.5, 3.0


def load(path):
    x, sr = sf.read(path, dtype="float64")
    assert sr == SR
    return x.mean(axis=1) if x.ndim == 2 else x


def yin(x, fmin=50.0, fmax=2500.0, win=2048, threshold=0.15):
    """Frame-wise YIN f0 (Hz, nan when unvoiced) and the aperiodicity measure."""
    tau_min, tau_max = int(SR / fmax), int(SR / fmin)
    frames = range(0, len(x) - win - tau_max, HOP)
    f0 = np.full(len(frames), np.nan)
    ap = np.ones(len(frames))
    for k, start in enumerate(frames):
        seg = x[start:start + win + tau_max]
        if np.sqrt(np.mean(seg[:win] ** 2)) < 1e-3:
            continue
        # Difference function via FFT autocorrelation.
        n = 1 << int(np.ceil(np.log2(len(seg) * 2)))
        # acf[tau] = sum_{j<win} seg[j] * seg[j + tau]
        acf = np.fft.irfft(np.fft.rfft(seg, n) * np.conj(np.fft.rfft(seg[:win], n)), n)[: tau_max + 1]
        energy = np.cumsum(np.concatenate([[0.0], seg ** 2]))
        e0 = energy[win] - energy[0]
        taus = np.arange(tau_max + 1)
        e_tau = energy[taus + win] - energy[taus]
        d = e0 + e_tau - 2 * acf
        cmnd = np.ones_like(d)
        cmnd[1:] = d[1:] * taus[1:] / np.maximum(np.cumsum(d[1:]), 1e-12)
        cand = np.where(cmnd[tau_min:] < threshold)[0]
        if len(cand) == 0:
            t = tau_min + int(np.argmin(cmnd[tau_min:]))
        else:
            t = tau_min + cand[0]
            while t + 1 <= tau_max and cmnd[t + 1] < cmnd[t]:
                t += 1
        if 1 <= t < tau_max:
            a, b, c = cmnd[t - 1], cmnd[t], cmnd[t + 1]
            denom = a - 2 * b + c
            shift = 0.5 * (a - c) / denom if denom != 0 else 0.0
        else:
            shift = 0.0
        ap[k] = cmnd[t]
        if cmnd[t] < 0.5:
            f0[k] = SR / (t + shift)
    times = (np.arange(len(f0)) * HOP + win / 2) / SR
    return times, f0, ap


def envelope(x, ms=5.0):
    n = int(SR * ms / 1000)
    rms = np.sqrt(np.convolve(x ** 2, np.ones(n) / n, mode="same"))
    return rms[::HOP]


def band_spectrum(x, t0, t1):
    seg = x[int(t0 * SR):int(t1 * SR)]
    f, p = sps.welch(seg, SR, nperseg=8192)
    edges = 31.25 * 2 ** (np.arange(0, 30) / 3)  # third-octave bands to ~16 kHz
    bands = [10 * np.log10(np.mean(p[(f >= lo) & (f < hi)]) + 1e-20) for lo, hi in zip(edges[:-1], edges[1:])]
    return edges[:-1].tolist(), bands


def peaks(x, t0, t1, count=6):
    seg = x[int(t0 * SR):int(t1 * SR)]
    seg = seg * np.hanning(len(seg))
    mag = np.abs(np.fft.rfft(seg, 1 << 18))
    freqs = np.fft.rfftfreq(1 << 18, 1 / SR)
    db = 20 * np.log10(mag / (mag.max() + 1e-20) + 1e-12)
    idx, _ = sps.find_peaks(db, height=-40, distance=int(3 / (freqs[1])))
    top = idx[np.argsort(db[idx])[::-1][:count]]
    return sorted([(round(float(freqs[i]), 2), round(float(db[i]), 1)) for i in top])


def am_rate(x, t0, t1):
    env = envelope(x[int(t0 * SR):int(t1 * SR)], ms=2.0)
    env = env - env.mean()
    if np.max(np.abs(env)) < 1e-6:
        return 0.0, 0.0
    f, p = sps.periodogram(env, SR / HOP)
    mask = (f > 0.2) & (f < 40)
    rate = float(f[mask][np.argmax(p[mask])])
    raw = envelope(x[int(t0 * SR):int(t1 * SR)], ms=2.0)
    depth = float((np.percentile(raw, 95) - np.percentile(raw, 5)) / (np.percentile(raw, 95) + 1e-12))
    return rate, depth


def features(path, case):
    x = load(path)
    out = {"rms_db": float(20 * np.log10(np.sqrt(np.mean(x ** 2)) + 1e-12))}
    times, f0, ap = yin(x)
    out["pitch"] = {"t": times.tolist(), "f0": [None if np.isnan(v) else float(v) for v in f0]}

    hold_end = RELEASE
    for sw in ("oct1", "oct2"):
        if case.get(sw):
            hold_end = case[sw][1]
    pre = (times > 0.1) & (times < PRESS - 0.02)
    hold = (times > PRESS + 0.6) & (times < hold_end - 0.05)
    base = np.nanmedian(f0[pre]) if np.any(~np.isnan(f0[pre])) else np.nan
    if np.any(hold) and np.any(~np.isnan(f0[hold])) and not np.isnan(base):
        cents = 1200 * np.log2(f0[hold] / base)
        cents = cents[~np.isnan(cents)]
        out["hold_shift_cents"] = float(np.median(cents))
        out["hold_spread_cents"] = float(np.std(cents))
        out["hold_voiced"] = float(np.mean(~np.isnan(f0[hold])))
        # Rise: time from press until pitch first reaches 90% of the hold shift.
        after = (times >= PRESS) & (times < hold_end)
        semis = 1200 * np.log2(f0[after] / base)
        goal = out["hold_shift_cents"]
        if abs(goal) > 100:
            reached = np.where(np.nan_to_num(semis, nan=0.0) >= 0.9 * goal)[0]
            if len(reached):
                out["rise_90_ms"] = float((times[after][reached[0]] - PRESS) * 1000)
    t0, t1 = PRESS + 0.6, hold_end - 0.05
    out["peaks"] = peaks(x, t0, t1)
    out["am_rate_hz"], out["am_depth"] = am_rate(x, t0, t1)
    out["bands_freq"], out["bands_db"] = band_spectrum(x, t0, t1)
    return out


def run_features(d):
    d = pathlib.Path(d)
    manifest = json.loads((d / "manifest.json").read_text())
    result = {c["name"]: features(d / f"{c['name']}.wav", c) for c in manifest["cases"]}
    (d / "features.json").write_text(json.dumps(result))
    return result


def latency(ref, test):
    """Lag (samples) of test relative to ref, from cross-correlation of the first second."""
    a, b = ref[: SR], test[: SR]
    corr = sps.correlate(b, a, mode="full", method="fft")
    return int(np.argmax(corr) - (len(a) - 1))


def compare(ref_dir, test_dir, out_dir, flt=None):
    ref_dir, test_dir, out_dir = map(pathlib.Path, (ref_dir, test_dir, out_dir))
    out_dir.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((ref_dir / "manifest.json").read_text())
    rows = []
    for c in manifest["cases"]:
        name = c["name"]
        if flt and flt not in name:
            continue
        if not (test_dir / f"{name}.wav").exists():
            continue
        fr = features(ref_dir / f"{name}.wav", c)
        ft = features(test_dir / f"{name}.wav", c)
        # Band distance over bands within 60 dB of the loudest band (ignores empty bands on pure tones).
        br, bt = np.array(fr["bands_db"]), np.array(ft["bands_db"])
        live = np.maximum(br, bt) > max(br.max(), bt.max()) - 60
        lsd = float(np.sqrt(np.mean((br[live] - bt[live]) ** 2)))
        rows.append({
            "case": name,
            "shift": (fr.get("hold_shift_cents"), ft.get("hold_shift_cents")),
            "spread": (fr.get("hold_spread_cents"), ft.get("hold_spread_cents")),
            "rise_ms": (fr.get("rise_90_ms"), ft.get("rise_90_ms")),
            "am": ((fr["am_rate_hz"], fr["am_depth"]), (ft["am_rate_hz"], ft["am_depth"])),
            "rms_db": (fr["rms_db"], ft["rms_db"]),
            "band_dist_db": lsd,
            "peaks": (fr["peaks"], ft["peaks"]),
        })
        plot_case(name, ref_dir / f"{name}.wav", test_dir / f"{name}.wav", fr, ft, out_dir / f"{name}.png")

    def fmt(v):
        if v is None:
            return "–"
        if isinstance(v, float):
            return f"{v:.1f}"
        return str(v)

    lines = ["| case | shift ¢ ref/calm | spread ¢ | rise ms | AM Hz/depth | RMS dB | band Δ dB |", "|---|---|---|---|---|---|---|"]
    for r in rows:
        am = f"{r['am'][0][0]:.1f}/{r['am'][0][1]:.2f} · {r['am'][1][0]:.1f}/{r['am'][1][1]:.2f}"
        lines.append(f"| {r['case']} | {fmt(r['shift'][0])} / {fmt(r['shift'][1])} | {fmt(r['spread'][0])} / {fmt(r['spread'][1])} | "
                     f"{fmt(r['rise_ms'][0])} / {fmt(r['rise_ms'][1])} | {am} | {fmt(r['rms_db'][0])} / {fmt(r['rms_db'][1])} | {r['band_dist_db']:.1f} |")
    (out_dir / "report.md").write_text("\n".join(lines) + "\n")
    (out_dir / "report.json").write_text(json.dumps(rows, indent=1))
    print("\n".join(lines))


def plot_case(name, ref_path, test_path, fr, ft, png):
    xr, xt = load(ref_path), load(test_path)
    fig, ax = plt.subplots(4, 1, figsize=(11, 11), constrained_layout=True)
    for x, label, a in ((xr, "Reference", ax[0]), (xt, "The Calm", ax[1])):
        f, t, s = sps.spectrogram(x, SR, nperseg=2048, noverlap=1536)
        a.pcolormesh(t, f, 10 * np.log10(s + 1e-12), shading="auto", vmin=-120, vmax=-20, cmap="magma")
        a.set_ylim(0, 4000)
        a.set_title(f"{label} — {name}")
    for feat, label in ((fr, "Reference"), (ft, "The Calm")):
        ax[2].plot(feat["pitch"]["t"], [np.nan if v is None else v for v in feat["pitch"]["f0"]], label=label, lw=1)
    ax[2].set_yscale("log")
    ax[2].set_ylabel("f0 Hz")
    ax[2].legend()
    ax[3].plot(np.arange(len(envelope(xr))) * HOP / SR, 20 * np.log10(envelope(xr) + 1e-9), label="Reference", lw=1)
    ax[3].plot(np.arange(len(envelope(xt))) * HOP / SR, 20 * np.log10(envelope(xt) + 1e-9), label="The Calm", lw=1)
    ax[3].set_ylim(-80, 0)
    ax[3].set_ylabel("RMS dB")
    ax[3].legend()
    fig.savefig(png, dpi=80)
    plt.close(fig)


if __name__ == "__main__":
    if sys.argv[1] == "features":
        run_features(sys.argv[2])
    else:
        compare(*sys.argv[2:6])

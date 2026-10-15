"""Compare real-material captures (DI guitar) between two directories.

    python compare_di.py <ref_dir> <test_dir> [filter]

Per case: RMS difference, third-octave band distance on the long-term spectrum, the same
distance averaged over 100 ms frames (catches modulation/texture differences), and the
correlation of the 10 ms loudness envelopes.
"""
import pathlib
import sys

import numpy as np
import soundfile as sf
from scipy import signal as sps

SR = 48000
EDGES = 50.0 * 2 ** (np.arange(0, 27) / 3)  # 50 Hz .. 20 kHz


def load(p):
    x, sr = sf.read(p, dtype="float64")
    assert sr == SR
    return x.mean(axis=1) if x.ndim == 2 else x


def band_matrix(x, frame=4800):
    f, t, s = sps.spectrogram(x, SR, nperseg=frame, noverlap=0)
    rows = [s[(f >= lo) & (f < hi)].mean(axis=0) for lo, hi in zip(EDGES[:-1], EDGES[1:])]
    return 10 * np.log10(np.array(rows) + 1e-20)


def envelope(x, n=480):
    m = len(x) // n
    return 20 * np.log10(np.sqrt((x[: m * n].reshape(m, n) ** 2).mean(axis=1)) + 1e-9)


def compare(a, b):
    n = min(len(a), len(b))
    a, b = a[:n], b[:n]
    rms = lambda x: 20 * np.log10(np.sqrt(np.mean(x ** 2)) + 1e-12)
    A, B = band_matrix(a), band_matrix(b)
    longa, longb = 10 * np.log10(np.mean(10 ** (A / 10), axis=1)), 10 * np.log10(np.mean(10 ** (B / 10), axis=1))
    live = np.maximum(longa, longb) > max(longa.max(), longb.max()) - 60
    long_dist = np.sqrt(np.mean((longa[live] - longb[live]) ** 2))
    loud = np.maximum(A, B) > np.max(np.maximum(A, B)) - 60
    frame_dist = np.sqrt(np.mean((A - B)[loud] ** 2))
    ea, eb = envelope(a), envelope(b)
    keep = np.maximum(ea, eb) > -60
    corr = np.corrcoef(ea[keep], eb[keep])[0, 1]
    return rms(a) - rms(b), long_dist, frame_dist, corr, longa - longb


def main():
    ref, test = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
    flt = sys.argv[3] if len(sys.argv) > 3 else ""
    rows = []
    for p in sorted(ref.glob("*.wav")):
        if flt not in p.name or not (test / p.name).exists():
            continue
        d_rms, d_long, d_frame, corr, tilt = compare(load(p), load(test / p.name))
        rows.append((p.stem, d_rms, d_long, d_frame, corr, tilt))
        print(f"{p.stem:40s} ΔRMS {d_rms:+5.1f} dB  long {d_long:4.1f}  frames {d_frame:4.1f}  env r {corr:.2f}", flush=True)
    if rows:
        arr = np.array([r[1:5] for r in rows])
        print(f"{'MEDIAN':40s} ΔRMS {np.median(np.abs(arr[:, 0])):5.2f}     long {np.median(arr[:, 1]):4.2f}  "
              f"frames {np.median(arr[:, 2]):4.2f}  env r {np.median(arr[:, 3]):.2f}")
        tilt = np.mean([r[5] for r in rows], axis=0)
        print("mean ref-test tilt by band (Hz:dB):",
              " ".join(f"{int(f)}:{d:+.1f}" for f, d in zip(EDGES[:-1:3], tilt[::3])))


if __name__ == "__main__":
    main()

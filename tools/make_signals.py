"""Generate the test signals used for A/B captures (48 kHz mono, float WAV)."""
import pathlib
import sys

import numpy as np
import soundfile as sf

SR = 48000
OUT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "reference/signals")


def sine(freq, seconds, amp=0.5):
    t = np.arange(int(seconds * SR)) / SR
    return amp * np.sin(2 * np.pi * freq * t)


def pluck(freq, seconds, amp=0.7, seed=1):
    """Karplus-Strong string, a stand-in for a DI guitar note."""
    rng = np.random.default_rng(seed)
    n = int(seconds * SR)
    period = SR / freq
    p = int(period)
    frac = period - p
    # Delay-line loop with fractional delay and gentle damping.
    line = np.concatenate([rng.uniform(-1, 1, p + 2), np.zeros(n)])
    for i in range(p + 2, p + 2 + n):
        a = line[i - p - 1]
        b = line[i - p]
        delayed = b + frac * (a - b)
        line[i] = 0.996 * 0.5 * (delayed + line[i - p - 1])
    out = line[p + 2 :]
    return amp * out / (np.max(np.abs(out)) + 1e-9)


def chord(freqs, seconds):
    sig = sum(pluck(f, seconds, seed=i + 2) for i, f in enumerate(freqs))
    return 0.7 * sig / np.max(np.abs(sig))


def impulses(every, seconds, amp=0.8):
    out = np.zeros(int(seconds * SR))
    out[int(0.25 * SR)::int(every * SR)] = amp
    return out


def filtered_noise(kind, hz, seconds=8.0):
    from scipy import signal as sps
    sos = sps.butter(8, hz, kind, fs=SR, output="sos")
    x = sps.sosfilt(sos, np.random.default_rng(9).standard_normal(int(seconds * SR)))
    return 0.25 * x / np.std(x)


def log_sweep(f0, f1, seconds, amp=0.5):
    t = np.arange(int(seconds * SR)) / SR
    k = np.log(f1 / f0)
    return amp * np.sin(2 * np.pi * f0 * seconds / k * (np.exp(t / seconds * k) - 1))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    signals = {
        "sine220": sine(220.0, 4.0),
        "sine82": sine(82.41, 4.0),
        "sine220_long": sine(220.0, 9.0),
        "sweep": log_sweep(40.0, 4000.0, 6.0),
        "pluckE2": pluck(82.41, 4.0),
        "pluckA3": pluck(220.0, 4.0),
        "power": chord([82.41, 123.47, 164.81], 4.0),
        "noise": 0.25 * np.random.default_rng(7).standard_normal(4 * SR),
        "quiet220": sine(220.0, 4.0, amp=0.01),
        # 10 periods per 2300-sample grain window: grain phase jumps vanish, so lines sit at the true ratios.
        "probe": sine(10 * SR / 2300, 6.0),
        "impulses": impulses(0.5, 4.0),
        "noise_m10": 0.079 * np.random.default_rng(7).standard_normal(4 * SR),
        "noise_m20": 0.025 * np.random.default_rng(7).standard_normal(4 * SR),
        "noise_m30": 0.0079 * np.random.default_rng(7).standard_normal(4 * SR),
        "sine220_loud": sine(220.0, 4.0, amp=1.0),
        "sine220_m20": sine(220.0, 4.0, amp=0.05),
        "sine2k": sine(2000.0, 4.0),
        "hisweep": log_sweep(500.0, 23000.0, 8.0, amp=0.3),
        "noise8": 0.25 * np.random.default_rng(8).standard_normal(8 * SR),
        "sine220_8": sine(220.0, 8.0),
        "noise_hp200": filtered_noise("highpass", 200.0),
        "noise_lp150": filtered_noise("lowpass", 150.0),
        "sine25": sine(25.0, 8.0),
        "sine40": sine(40.0, 8.0),
        "sine60": sine(60.0, 8.0),
    }
    for name, sig in signals.items():
        sf.write(OUT / f"{name}.wav", sig.astype(np.float32), SR, subtype="FLOAT")
    print(f"wrote {len(signals)} signals to {OUT}")


if __name__ == "__main__":
    main()

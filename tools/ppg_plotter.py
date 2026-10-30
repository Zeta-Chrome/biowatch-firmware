import re
import os
import argparse
import numpy as np
import matplotlib.pyplot as plt
from scipy.signal import butter, filtfilt

# ---- settings ----
parser = argparse.ArgumentParser(description="Plot PPG red/IR data: raw, DC removed, DC removed with gain.")
parser.add_argument("filename", help="Path to text file with 'red, ir' pairs (one per line)")
parser.add_argument("--fs", type=float, default=12.5, help="Sample rate in Hz (default: 12.5)")
parser.add_argument("--skip", type=int, default=3, help="Ignore first N samples, startup artifact (default: 3)")
parser.add_argument("--gain", type=float, default=20, help="Amplification for the AC signal (default: 20)")
args = parser.parse_args()

FILENAME = args.filename
FS = args.fs
SKIP = args.skip
GAIN = args.gain

# ---- parse file ----
red, ir = [], []
with open(FILENAME) as f:
    for line in f:
        m = re.search(r'(-?\d+)\s*,\s*(-?\d+)', line)
        if m:
            red.append(int(m.group(1)))
            ir.append(int(m.group(2)))

red = np.array(red[SKIP:], dtype=float)
ir = np.array(ir[SKIP:], dtype=float)
t = np.arange(len(red)) / FS

# ---- remove DC (bandpass 0.5-5 Hz keeps just the pulse band) ----
b, a = butter(3, [0.5 / (FS / 2), 5 / (FS / 2)], btype='band')
red_ac = filtfilt(b, a, red)
ir_ac = filtfilt(b, a, ir)

# ---- perfusion index (unamplified, real numbers) ----
red_pi = 100 * (red.max() - red.min()) / red.mean()
ir_pi = 100 * (ir.max() - ir.min()) / ir.mean()
print(f"Red  -> baseline {red.mean():.0f}, PI {red_pi:.2f}%")
print(f"IR   -> baseline {ir.mean():.0f}, PI {ir_pi:.2f}%")

# ---- plot ----
fig, axes = plt.subplots(2, 2, figsize=(12, 8), sharex=True)

axes[0, 0].plot(t, red, color='red')
axes[0, 0].set_title("Red - Raw")
axes[0, 0].grid(alpha=0.3)

axes[0, 1].plot(t, ir, color='darkviolet')
axes[0, 1].set_title("IR - Raw")
axes[0, 1].grid(alpha=0.3)

axes[1, 0].plot(t, red_ac * GAIN, color='red')
axes[1, 0].set_title(f"Red - DC removed x{GAIN} gain")
axes[1, 0].set_xlabel("Time (s)")
axes[1, 0].grid(alpha=0.3)

axes[1, 1].plot(t, ir_ac * GAIN, color='darkviolet')
axes[1, 1].set_title(f"IR - DC removed x{GAIN} gain")
axes[1, 1].set_xlabel("Time (s)")
axes[1, 1].grid(alpha=0.3)

plt.tight_layout()

# ---- save next to input file, with _fig.png suffix ----
base, _ = os.path.splitext(FILENAME)
out_path = base + "_fig.png"
plt.savefig(out_path, dpi=150)
print(f"Saved plot to {out_path}")

plt.show()

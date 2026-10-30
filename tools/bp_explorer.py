import re
import matplotlib.pyplot as plt
import numpy as np
from scipy.signal import butter, sosfilt

# ==============================================================================
# 1. READ LOG FILE AND ISOLATE RAW "Read SPO2" SECTION
# ==============================================================================
with open("log.txt", "r") as f:
    log_content = f.read()

# Isolate only the "Read SPO2" section (ignore BANDPASS and NLMS sections)
raw_section = log_content.split(
    "----------------------Read SPO2-----------------------"
)[1]
raw_section = raw_section.split(
    "-------------------BANDPASS DATA -------------------------"
)[0]

# Regex pattern for tuple matching: (red, ir), (acc_x, acc_y, acc_z)
pattern = re.compile(
    r"\(([^,]+),\s*([^)]+)\),\s*\(([^,]+),\s*([^,]+),\s*([^)]+)\)"
)

red_list, ir_list, ax_list, ay_list, az_list = [], [], [], [], []

for line in raw_section.strip().split("\n"):
    m = pattern.search(line)
    if m:
        r, i, ax, ay, az = map(float, m.groups())
        red_list.append(r)
        ir_list.append(i)
        ax_list.append(ax)
        ay_list.append(ay)
        az_list.append(az)

red = np.array(red_list)
ir = np.array(ir_list)
ax = np.array(ax_list)
ay = np.array(ay_list)
az = np.array(az_list)

fs = 25.0  # Sampling rate in Hz
time = np.arange(len(red)) / fs

# ==============================================================================
# 2. DESIGN BANDPASS FILTER (0.5 Hz - 4.0 Hz) & GENERATE ARM COEFFICIENTS
# ==============================================================================
lowcut, highcut = 0.7, 3.0
sos = butter(N=2, Wn=[lowcut, highcut], btype="bandpass", fs=fs, output="sos")
num_stages = len(sos)

# ARM CMSIS-DSP Biquad DF2T coefficient array ordering:
# {b10, b11, b12, a11, a12, b20, b21, b22, a21, a22, ...}
# Note: CMSIS-DSP uses +a1, +a2 in y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] + a1*y[n-1] + a2*y[n-2]
# Therefore, a1_cmsis = -a1_scipy and a2_cmsis = -a2_scipy
coefs = []
for stage in sos:
    b0, b1, b2, _, a1, a2 = stage
    coefs.extend([b0, b1, b2, -a1, -a2])

# Print C code declaration for standard ARM CMSIS-DSP array
print("=" * 70)
print("// ARM CMSIS-DSP Biquad Coefficients Array (float32_t)")
print("// Order: {b10, b11, b12, a11, a12, b20, b21, b22, a21, a22}")
print("=" * 70)
print(f"#define NUM_STAGES {num_stages}\n")
print(f"float32_t coefs[5 * NUM_STAGES] = {{")
for stage_idx in range(num_stages):
    stage_coefs = coefs[stage_idx * 5 : (stage_idx + 1) * 5]
    line = "    " + ", ".join([f"{c:12.8f}f" for c in stage_coefs])
    if stage_idx < num_stages - 1:
        line += ","
    print(line)
print("};\n")

# Print MCU initialization code snippet (Standard & Neon versions)
print("/* MCU C Code Initialization Snippet */")
print(
    """
// Standard (non-Neon) Version Initialization:
float32_t state[2 * NUM_STAGES] = {0.0f};
arm_biquad_cascade_df2T_instance_f32 S;
arm_biquad_cascade_df2T_init_f32(&S, NUM_STAGES, coefs, state);

// Neon Version Initialization (if using Helium / Neon vectorized version):
float32_t computedCoefs[8 * NUM_STAGES];
float32_t stateNeon[2 * NUM_STAGES] = {0.0f};
arm_biquad_cascade_df2T_instance_f32 SNeon;
arm_biquad_cascade_df2T_compute_coefs_f32(NUM_STAGES, coefs, computedCoefs);
arm_biquad_cascade_df2T_init_f32(&SNeon, NUM_STAGES, computedCoefs, stateNeon);
"""
)
print("=" * 70)

# ==============================================================================
# 3. APPLY FILTER & PLOT ALL DATA
# ==============================================================================
red_filtered = sosfilt(sos, red - np.mean(red))
ir_filtered = sosfilt(sos, ir - np.mean(ir))

fig, axes = plt.subplots(3, 1, figsize=(10, 8), sharex=True)

# Subplot 1: Raw PPG
axes[0].plot(time, red, label="Red Channel", color="#d62728", linewidth=1.5)
axes[0].plot(time, ir, label="IR Channel", color="#1f77b4", linewidth=1.5)
axes[0].set_title("Raw PPG Signals (Read SPO2 Section)", fontweight="bold")
axes[0].set_ylabel("Amplitude (V / ADC)")
axes[0].legend(loc="upper right")
axes[0].grid(True, linestyle="--", alpha=0.5)

# Subplot 2: Bandpass Filtered PPG (0.5 - 4 Hz)
axes[1].plot(
    time,
    red_filtered,
    label="Filtered Red (0.5 - 4 Hz)",
    color="#e377c2",
    linewidth=1.5,
)
axes[1].plot(
    time,
    ir_filtered,
    label="Filtered IR (0.5 - 4 Hz)",
    color="#2ca02c",
    linewidth=1.5,
)
axes[1].set_title(
    "2-Stage Biquad Bandpass Filtered PPG (0.5 Hz - 4.0 Hz)", fontweight="bold"
)
axes[1].set_ylabel("Filtered Amplitude")
axes[1].legend(loc="upper right")
axes[1].grid(True, linestyle="--", alpha=0.5)

# Subplot 3: Accelerometer Channels
axes[2].plot(time, ax, label="Acc X", color="#ff7f0e", alpha=0.75)
axes[2].plot(time, ay, label="Acc Y", color="#bcbd22", alpha=0.75)
axes[2].plot(time, az, label="Acc Z", color="#17becf", alpha=0.75)
axes[2].set_title("Accelerometer Data (X, Y, Z)", fontweight="bold")
axes[2].set_xlabel("Time (seconds)")
axes[2].set_ylabel("Acceleration (g)")
axes[2].legend(loc="upper right")
axes[2].grid(True, linestyle="--", alpha=0.5)

plt.tight_layout()
plt.show()

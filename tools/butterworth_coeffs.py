import numpy as np
from scipy.signal import butter, sosfilt, sosfreqz

# ---------- Filter design ----------
FS = 25.0          # sample rate (Hz)
LOW = 0.5          # bandpass low edge (Hz)
HIGH = 4.0         # bandpass high edge (Hz)
ORDER = 2          # -> 2 biquad stages for bandpass

sos = butter(ORDER, [LOW, HIGH], btype='band', fs=FS, output='sos')
print("Float SOS (b0,b1,b2,a0,a1,a2) per stage:")
print(sos)

# ---------- Build CMSIS f32 coefficient array ----------
# arm_biquad_cascade_df2T_f32 expects, per stage:
#   {b0, b1, b2, a1, a2}
# with the SAME sign convention as scipy's sos (1 + a1*z^-1 + a2*z^-2),
# so a1/a2 are used as-is, NO negation needed for the df2T f32 variant.
# (Unlike the q31 df1 path, there's no postShift/scaling step here.)

f32_coeffs = []
for stage in sos:
    b0, b1, b2, a0, a1, a2 = stage
    f32_coeffs += [b0, b1, b2, a1, a2]

print("\nFloat32 coefficient array (b10,b11,b12,a11,a12, b20,b21,b22,a21,a22):")
print(", ".join(f"{c:.10f}f" for c in f32_coeffs))

# ---------- C header output ----------
c_array = ", ".join(f"{c:.10f}f" for c in f32_coeffs)
num_stages = len(sos)
print(f"""
// ---- paste into your C source ----
#define BANDPASS_NUM_STAGES {num_stages}
const float32_t bandpass_coeffs_f32[5 * BANDPASS_NUM_STAGES] = {{
    {c_array}
}};
""")

# ---------- Validation: simulate CMSIS df2T behavior, compare to scipy reference ----------
def simulate_df2t_f32(coeffs, x, num_stages):
    """
    Direct Form II Transposed, matching arm_biquad_cascade_df2T_f32 structure:
        y[n]  = b0*x[n] + d1
        d1    = b1*x[n] - a1*y[n] + d2
        d2    = b2*x[n] - a2*y[n]
    per stage, output of one stage feeds the next.
    """
    y = np.array(x, dtype=np.float64)
    for s in range(num_stages):
        b0, b1, b2, a1, a2 = coeffs[s*5:(s+1)*5]
        xin = y.copy()
        yout = np.zeros_like(xin)
        d1 = 0.0
        d2 = 0.0
        for n in range(len(xin)):
            xn = xin[n]
            yn = b0*xn + d1
            d1 = b1*xn - a1*yn + d2
            d2 = b2*xn - a2*yn
            yout[n] = yn
        y = yout
    return y

# test signal: mix of in-band (1.2Hz, mimics HR) and out-of-band (0.1Hz drift + 8Hz noise)
t = np.arange(0, 20, 1/FS)
test = (np.sin(2*np.pi*1.2*t) +          # in-band, should pass
        0.5*np.sin(2*np.pi*0.1*t) +      # below band, should attenuate
        0.3*np.sin(2*np.pi*8*t))         # above band, should attenuate

ref = sosfilt(sos, test)
sim = simulate_df2t_f32(f32_coeffs, test, num_stages)

err = np.abs(ref - sim)
print(f"Max abs error between scipy reference and simulated CMSIS df2T: {err.max():.10f}")
print(f"(reference signal peak amplitude: {np.abs(ref).max():.4f})")

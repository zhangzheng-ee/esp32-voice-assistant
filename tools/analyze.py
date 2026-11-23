# -*- coding: utf-8 -*-
import wave
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

plt.rcParams['font.sans-serif'] = ['Microsoft YaHei', 'SimHei']
plt.rcParams['axes.unicode_minus'] = False

SRC = r"D:\zz187\Arduino\audio_src\guoxin.wav"
OUT = r"D:\zz187\Arduino\audio_src\analysis.png"

w = wave.open(SRC, 'rb')
n = w.getnframes()
sr = w.getframerate()
ch = w.getnchannels()
sw = w.getsampwidth()
raw = w.readframes(n)
w.close()

x = np.frombuffer(raw, dtype=np.int16).astype(np.float64)
if ch > 1:
    x = x.reshape(-1, ch).mean(axis=1)

dur = len(x) / sr
t = np.arange(len(x)) / sr

peak = np.max(np.abs(x))
rms = np.sqrt(np.mean(x ** 2))
clip = int(np.sum(np.abs(x) >= 32700))

print(f"时长 {dur:.3f}s / {sr}Hz / {ch}ch / {sw*8}bit / {len(x)} 点")
print(f"峰值 {peak:.0f} = {peak/32768*100:.2f}% FS")
print(f"RMS  {rms:.0f} = {rms/32768*100:.2f}% FS")
print(f"削顶样本 {clip}")

# 每 50ms 能量
seg = int(sr * 0.05)
segs = len(x) // seg
seg_rms = np.array([np.sqrt(np.mean(x[i*seg:(i+1)*seg] ** 2)) for i in range(segs)]) if segs else np.array([0])
quiet = int(np.sum(seg_rms < rms * 0.2)) if rms > 0 else 0
print(f"静音段数(低于RMS20%) {quiet} / {segs}")

# FFT
N = min(len(x), sr * 2)
y = x[:N] * np.hanning(N)
Y = np.abs(np.fft.rfft(y))
freqs = np.fft.rfftfreq(N, 1 / sr)
idx = np.argsort(Y)[-12:][::-1]
print("\n主频峰 (前12):")
for i in idx:
    print(f"  {freqs[i]:8.1f} Hz   幅度 {Y[i]:.0f}")

# 谐波检查：找最大的非直流峰作为基频
mask = freqs > 40
if np.any(mask):
    f0 = freqs[mask][np.argmax(Y[mask])]
    print(f"\n基频约 {f0:.1f} Hz")
    for k in range(2, 6):
        tgt = f0 * k
        near = np.argmin(np.abs(freqs - tgt))
        amp = Y[near]
        base = Y[np.argmin(np.abs(freqs - f0))]
        print(f"  {k}次谐波 {tgt:7.1f} Hz  幅度比 {amp/base*100:6.1f}%")

fig, axes = plt.subplots(4, 1, figsize=(12, 11))

ax = axes[0]
ax.plot(t, x, lw=0.4, color='#2563eb')
ax.set_title(f"波形全貌 ({dur:.2f}s)  峰值 {peak/32768*100:.1f}% FS  RMS {rms/32768*100:.1f}% FS")
ax.set_xlabel("时间 (s)"); ax.set_ylabel("幅度")
ax.grid(alpha=0.3)

ax = axes[1]
sub = min(len(x), int(sr * 0.15))
ax.plot(t[:sub] * 1000, x[:sub], lw=1.0, color='#dc2626')
ax.set_title("局部放大 (前 150ms) — 看波形形状与削顶")
ax.set_xlabel("时间 (ms)"); ax.set_ylabel("幅度")
ax.grid(alpha=0.3)

ax = axes[2]
ax.semilogy(freqs, Y + 1e-6, lw=0.6, color='#059669')
ax.set_xlim(0, 8000)
ax.set_title("频谱 0–8kHz")
ax.set_xlabel("频率 (Hz)"); ax.set_ylabel("幅度")
ax.grid(alpha=0.3, which='both')

ax = axes[3]
tt = np.arange(segs) * 0.05
ax.bar(tt, seg_rms, width=0.045, color='#7c3aed')
ax.set_title("每 50ms 能量 — 看连续性与断续")
ax.set_xlabel("时间 (s)"); ax.set_ylabel("RMS")
ax.grid(alpha=0.3)

plt.tight_layout()
plt.savefig(OUT, dpi=110, bbox_inches='tight')
print(f"\n图已保存: {OUT}")
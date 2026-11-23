# -*- coding: utf-8 -*-
"""
ESP32 I2S 音质分析器
用法: python audio_check.py <wav文件>
输出: 控制台报告 + 同名 _分析.png
"""
import sys, wave
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

plt.rcParams['font.sans-serif'] = ['Microsoft YaHei', 'SimHei']
plt.rcParams['axes.unicode_minus'] = False

if len(sys.argv) < 2:
    print("用法: python audio_check.py <wav>")
    sys.exit(1)

SRC = sys.argv[1]
OUT = SRC.rsplit('.', 1)[0] + "_分析.png"

w = wave.open(SRC, 'rb')
n, sr, ch, sw = w.getnframes(), w.getframerate(), w.getnchannels(), w.getsampwidth()
raw = w.readframes(n)
w.close()

x = np.frombuffer(raw, dtype=np.int16).astype(np.float64)
if ch > 1:
    x = x.reshape(-1, ch).mean(axis=1)

dur = len(x) / sr
t = np.arange(len(x)) / sr
peak, rms = np.max(np.abs(x)), np.sqrt(np.mean(x ** 2))
clip = int(np.sum(np.abs(x) >= 32700))

print("=" * 46)
print(f"文件      {SRC}")
print(f"时长      {dur:.3f} 秒")
print(f"采样率    {sr} Hz   声道 {ch}   位深 {sw*8}")
print(f"峰值      {peak:.0f}  =  {peak/32768*100:.2f}% FS")
print(f"RMS       {rms:.0f}  =  {rms/32768*100:.2f}% FS")
print(f"削顶样本  {clip}   {'⚠ 有削顶!' if clip > 0 else '✅ 无削顶'}")

seg = int(sr * 0.05)
segs = max(1, len(x) // seg)
seg_rms = np.array([np.sqrt(np.mean(x[i*seg:(i+1)*seg]**2)) for i in range(segs)])
quiet_n = int(np.sum(seg_rms < rms * 0.2)) if rms > 0 else 0
print(f"静音段    {quiet_n}/{segs}  ({quiet_n/segs*100:.0f}%)")

N = min(len(x), sr * 2)
Y = np.abs(np.fft.rfft(x[:N] * np.hanning(N)))
freqs = np.fft.rfftfreq(N, 1 / sr)

mask = freqs > 40
f0 = freqs[mask][np.argmax(Y[mask])]
base = Y[np.argmin(np.abs(freqs - f0))]

print(f"\n基频      {f0:.1f} Hz   ({'音名≈A5' if 850<f0<910 else '见频谱'})")
print("谐波检查（越低越纯净，理想 <1%）:")
for k in range(2, 6):
    tgt = f0 * k
    near = np.argmin(np.abs(freqs - tgt))
    print(f"  {k}次 {tgt:7.1f}Hz   幅度比 {Y[near]/base*100:6.2f}%")

snr = 20 * np.log10(base / (np.median(Y[mask]) + 1e-9))
print(f"\n信噪比    {snr:.1f} dB   {'✅ 很干净' if snr > 30 else '一般' if snr > 20 else '⚠ 杂音大'}")

print("\n结论:")
verdict = []
verdict.append("✅ 频率正常" if 100 < f0 < 12000 else "⚠ 频率异常")
verdict.append("✅ 无削顶失真" if clip == 0 else f"⚠ 削顶 {clip} 点")
verdict.append("✅ 谐波极低，纯正弦" if Y[np.argmin(np.abs(freqs-f0*2))]/base < 0.05 else "⚠ 谐波偏高，有失真")
verdict.append(f"音量 {peak/32768*100:.0f}% FS" + ("（偏小，可加大）" if peak/32768 < 0.5 else "（合适）"))
for v in verdict:
    print("  " + v)

fig, axes = plt.subplots(4, 1, figsize=(12, 11))
axes[0].plot(t, x, lw=0.4, color='#2563eb')
axes[0].set_title(f"波形全貌 ({dur:.2f}s)  峰值 {peak/32768*100:.1f}%  RMS {rms/32768*100:.1f}%")
axes[0].set_xlabel("时间 (s)"); axes[0].grid(alpha=0.3)

sub = min(len(x), int(sr * 0.15))
axes[1].plot(t[:sub] * 1000, x[:sub], lw=1.0, color='#dc2626')
axes[1].set_title("局部放大 (前150ms)")
axes[1].set_xlabel("时间 (ms)"); axes[1].grid(alpha=0.3)

axes[2].semilogy(freqs, Y + 1e-6, lw=0.6, color='#059669')
axes[2].set_xlim(0, 8000); axes[2].set_title("频谱 0–8kHz")
axes[2].set_xlabel("频率 (Hz)"); axes[2].grid(alpha=0.3, which='both')

axes[3].bar(np.arange(segs) * 0.05, seg_rms, width=0.045, color='#7c3aed')
axes[3].set_title("每50ms能量")
axes[3].set_xlabel("时间 (s)"); axes[3].grid(alpha=0.3)

plt.tight_layout()
plt.savefig(OUT, dpi=110, bbox_inches='tight')
print(f"\n图: {OUT}")
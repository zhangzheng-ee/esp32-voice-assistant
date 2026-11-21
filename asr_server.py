#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ASR HTTP 服务：收 wav/PCM -> faster-whisper 转文字 -> 返回 JSON
启动: python asr_server.py
测试: curl -X POST --data-binary @test.wav http://127.0.0.1:8080/asr

注意：不用 av 解码（版本不兼容会报 metadata_errors），
     自己用 wave+numpy 解析，绕开依赖。
"""
import io
import os
import sys
import json
import wave
import time
import threading
import numpy as np
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

os.environ.setdefault("HF_ENDPOINT", "https://hf-mirror.com")

MODEL_SIZE = os.environ.get("ASR_MODEL", "small")
PORT = int(os.environ.get("ASR_PORT", "8080"))
TARGET_SR = 16000

print(f"[asr] loading model={MODEL_SIZE} ...", flush=True)
from faster_whisper import WhisperModel
from opencc import OpenCC

cc = OpenCC("t2s")   # 繁体 -> 简体

_t0 = time.time()
model = WhisperModel(
    MODEL_SIZE,
    device="cpu",
    compute_type="int8",
    download_root=r"D:\zz187\models",
)
print(f"[asr] model ready in {time.time()-_t0:.1f}s", flush=True)

_lock = threading.Lock()


def resample(x: np.ndarray, sr_in: int, sr_out: int) -> np.ndarray:
    if sr_in == sr_out or len(x) == 0:
        return x.astype(np.float32)
    n_out = max(1, int(len(x) * sr_out / sr_in))
    idx = np.linspace(0, len(x) - 1, n_out)
    return np.interp(idx, np.arange(len(x)), x).astype(np.float32)


def decode(data: bytes, hint_rate: int):
    """返回 (float32 mono array, sr)"""
    if data[:4] == b"RIFF":
        with wave.open(io.BytesIO(data), "rb") as w:
            nch = w.getnchannels()
            sw = w.getsampwidth()
            sr = w.getframerate()
            raw = w.readframes(w.getnframes())
        if sw == 2:
            arr = np.frombuffer(raw, dtype=np.int16).astype(np.float32) / 32768.0
        elif sw == 1:
            arr = (np.frombuffer(raw, dtype=np.uint8).astype(np.float32) - 128.0) / 128.0
        elif sw == 4:
            arr = np.frombuffer(raw, dtype=np.int32).astype(np.float32) / 2147483648.0
        else:
            raise ValueError(f"unsupported sampwidth={sw}")
        if nch > 1:
            arr = arr.reshape(-1, nch).mean(axis=1)
    else:
        sr = hint_rate
        arr = np.frombuffer(data[: len(data) // 2 * 2], dtype=np.int16).astype(np.float32) / 32768.0
    return arr, sr


def transcribe(data: bytes, hint_rate: int):
    if len(data) < 100:
        return {"ok": False, "error": "too short"}
    arr, sr = decode(data, hint_rate)
    if len(arr) < sr // 4:
        return {"ok": False, "error": "audio too short (<0.25s)"}

    arr = resample(arr, sr, TARGET_SR)

    t0 = time.time()
    with _lock:
        segments, info = model.transcribe(
            arr,
            language="zh",
            beam_size=1,
            vad_filter=True,
            vad_parameters=dict(min_silence_duration_ms=300),
            condition_on_previous_text=False,
        )
        text = "".join(s.text for s in segments).strip()
        text = cc.convert(text)
        # 只剩标点/空白 = 没说话
        if not any(("\u4e00" <= ch <= "\u9fff") or ch.isalnum() for ch in text):
            text = ""
    return {
        "ok": True,
        "text": text,
        "lang": info.language,
        "dur": round(info.duration, 2),
        "ms": int((time.time() - t0) * 1000),
    }


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        sys.stderr.write("[http] " + fmt % args + "\n")

    def _send(self, code: int, obj):
        body = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/ping":
            self._send(200, {"ok": True, "model": MODEL_SIZE})
        else:
            self._send(404, {"ok": False, "error": "not found"})

    def do_POST(self):
        if self.path != "/asr":
            self._send(404, {"ok": False, "error": "not found"})
            return
        try:
            n = int(self.headers.get("Content-Length", 0))
            rate = int(self.headers.get("X-Sample-Rate", "16000"))
            data = self.rfile.read(n) if n > 0 else b""
            r = transcribe(data, rate)
            self._send(200 if r.get("ok") else 400, r)
            print(f"[asr] {r}", flush=True)
        except Exception as e:
            import traceback
            traceback.print_exc()
            self._send(500, {"ok": False, "error": str(e)})


if __name__ == "__main__":
    srv = ThreadingHTTPServer(("0.0.0.0", PORT), Handler)
    print(f"[asr] listening 0.0.0.0:{PORT}  POST /asr  GET /ping", flush=True)
    srv.serve_forever()
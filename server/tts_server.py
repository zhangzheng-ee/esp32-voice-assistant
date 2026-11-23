#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
TTS 服务 v2：Edge TTS（微软神经网络音色）
POST /tts  body=text/plain  ->  audio/wav (22050Hz mono 16bit)
GET  /ping -> ok
GET  /voices -> 可选音色列表
"""
import os, sys, io, json, wave, struct, asyncio, tempfile, subprocess, threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import edge_tts

PORT = 8081
DEFAULT_VOICE = "zh-CN-XiaoxiaoNeural"
TMP = tempfile.gettempdir()

VOICES = {
    "xiaoxiao": "zh-CN-XiaoxiaoNeural",   # 女声，温柔自然（默认）
    "yunxi":    "zh-CN-YunxiNeural",      # 男声，年轻
    "xiaoyi":   "zh-CN-XiaoyiNeural",     # 女声，活泼
    "yunjian":  "zh-CN-YunjianNeural",    # 男声，沉稳
    "xiaobei":  "zh-CN-liaoning-XiaobeiNeural",  # 东北话
}

def find_ffmpeg():
    try:
        import imageio_ffmpeg
        return imageio_ffmpeg.get_ffmpeg_exe()
    except Exception:
        return "ffmpeg"

FFMPEG = find_ffmpeg()
_lock = threading.Lock()

def synth(text: str, voice: str = DEFAULT_VOICE) -> bytes:
    """edge-tts 合成 mp3 -> ffmpeg 转 22050Hz mono 16bit wav -> bytes"""
    with _lock:
        mp3 = os.path.join(TMP, "tts_edge.mp3")
        wav = os.path.join(TMP, "tts_edge.wav")
        for f in (mp3, wav):
            if os.path.exists(f):
                try: os.remove(f)
                except Exception: pass

        # 1) 合成 mp3
        async def _do():
            c = edge_tts.Communicate(text, voice)
            await c.save(mp3)
        asyncio.run(_do())

        if not os.path.exists(mp3) or os.path.getsize(mp3) == 0:
            raise RuntimeError("edge-tts 生成失败")

        # 2) 转 wav
        cmd = [FFMPEG, "-y", "-loglevel", "error", "-i", mp3,
               "-ar", "22050", "-ac", "1", "-sample_fmt", "s16", wav]
        r = subprocess.run(cmd, capture_output=True)
        if r.returncode != 0 or not os.path.exists(wav):
            raise RuntimeError("ffmpeg 转码失败: " + r.stderr.decode("utf-8", "ignore")[:200])

        with open(wav, "rb") as f:
            return f.read()

class H(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def _send(self, code, body: bytes, ctype="text/plain; charset=utf-8"):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path.startswith("/ping"):
            self._send(200, b"ok")
        elif self.path.startswith("/voices"):
            body = json.dumps(VOICES, ensure_ascii=False).encode("utf-8")
            self._send(200, body, "application/json; charset=utf-8")
        else:
            self._send(404, b"404")

    def do_POST(self):
        if not self.path.startswith("/tts"):
            self._send(404, b"404")
            return
        n = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(n) if n > 0 else b""
        text = raw.decode("utf-8", "ignore").strip()
        if not text:
            self._send(400, b"empty text")
            return

        # 音色：?voice=yunxi 或 header X-Voice
        vk = (self.headers.get("X-Voice") or "").strip().lower()
        if "voice=" in self.path:
            vk = self.path.split("voice=")[1].split("&")[0].strip().lower()
        voice = VOICES.get(vk, DEFAULT_VOICE)

        try:
            data = synth(text, voice)
            print(f"[tts] '{text[:20]}' voice={voice} -> {len(data)} bytes", flush=True)
            self._send(200, data, "audio/wav")
        except Exception as e:
            print(f"[tts] ERR {e}", flush=True)
            self._send(500, str(e).encode("utf-8"))

if __name__ == "__main__":
    print(f"[tts] Edge TTS server on :{PORT}  ffmpeg={FFMPEG}", flush=True)
    print(f"[tts] voices: {list(VOICES.keys())}", flush=True)
    ThreadingHTTPServer(("0.0.0.0", PORT), H).serve_forever()
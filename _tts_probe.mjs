import { synthesizeToFile } from 'file:///D:/qq-bridge/src/tts.mjs';
try {
  const r = await synthesizeToFile('测试一句，看还报不报 418。', 'D:/zz187/Arduino/audio_src/_probe.wav', {
    voice: 'Serena',
    model: 'qwen3-tts-flash',
    cacheDir: 'D:/qq-bridge/audio/_tts'
  });
  console.log('OK  bytes=' + r.bytes + '  via=' + (r.via || 'http') + '  voice=' + r.voice);
} catch (e) {
  console.log('FAIL  ' + (e?.message || e));
}
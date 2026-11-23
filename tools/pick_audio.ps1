Add-Type -AssemblyName System.Windows.Forms
$ErrorActionPreference = "Stop"

$py = "C:\Users\zz187\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe"
$wav2h = "D:\zz187\Arduino\tools\wav2h.py"
$outH = "D:\zz187\Arduino\experiments\plantest\audio.h"
$tmpWav = "D:\zz187\Arduino\audio_src\_tmp_convert.wav"

$dlg = New-Object System.Windows.Forms.OpenFileDialog
$dlg.Title = "Choose audio -> audio.h"
$dlg.Filter = "Audio files|*.mp3;*.wav;*.m4a;*.flac;*.ogg;*.aac;*.wma;*.opus|All files|*.*"

if ($dlg.ShowDialog() -ne "OK") { exit }

$src = $dlg.FileName
Write-Host ""
Write-Host "Source : $src"

$ff = & $py -c "import imageio_ffmpeg; print(imageio_ffmpeg.get_ffmpeg_exe())"
if (-not (Test-Path $ff)) {
    Write-Host "[X] ffmpeg not found" -ForegroundColor Red
    Read-Host "Press Enter to close"
    exit
}

Write-Host "[1/2] Converting to 22050Hz mono 16bit ..."
& $ff -hide_banner -loglevel error -y -i $src -ar 22050 -ac 1 -c:a pcm_s16le $tmpWav
if ($LASTEXITCODE -ne 0) {
    Write-Host "[X] ffmpeg failed" -ForegroundColor Red
    Read-Host "Press Enter to close"
    exit
}

Write-Host "[2/2] Building audio.h ..."
& $py $wav2h $tmpWav $outH

Write-Host ""
Write-Host "==============================================" -ForegroundColor Green
Write-Host " audio.h updated. Go to Arduino IDE and click Upload." -ForegroundColor Green
Write-Host "==============================================" -ForegroundColor Green
Read-Host "Press Enter to close"
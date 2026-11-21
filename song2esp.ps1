# song2esp.ps1 - pick audio -> convert to 22050Hz mono s16le PCM -> pack LittleFS -> flash to 0x610000
$ErrorActionPreference = "Stop"

$DataDir = "D:\zz187\Arduino\plantest\data"
$ImgFile = "D:\zz187\Arduino\littlefs.bin"
$PartSize = 0x700000
$FlashAddr = "0x610000"

$Py   = "C:\Users\zz187\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe"
$Mk   = "C:\Users\zz187\AppData\Local\Arduino15\packages\esp32\tools\mklittlefs\3.0.0-gnu12-dc7f933-cn\mklittlefs.exe"
$Esp  = "C:\Users\zz187\AppData\Local\Arduino15\packages\esp32\tools\esptool_py\5.0.dev1-cn\esptool.exe"

Write-Host ""
Write-Host "=== STEP 1/4 : pick audio file ===" -ForegroundColor Cyan
Add-Type -AssemblyName System.Windows.Forms
$dlg = New-Object System.Windows.Forms.OpenFileDialog
$dlg.Title = "Pick audio (mp3 / wav / m4a / flac / ogg)"
$dlg.Filter = "Audio|*.mp3;*.wav;*.m4a;*.flac;*.ogg;*.aac;*.wma|All files|*.*"
if ($dlg.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) {
    Write-Host "cancelled."
    Start-Sleep -Seconds 2
    exit
}
$src = $dlg.FileName
Write-Host "source: $src"

Write-Host ""
Write-Host "=== STEP 2/4 : convert to PCM ===" -ForegroundColor Cyan
if (-not (Test-Path $DataDir)) { New-Item -ItemType Directory -Force -Path $DataDir | Out-Null }
$outPcm = Join-Path $DataDir "song.pcm"

$ff = & $Py -c "import imageio_ffmpeg; print(imageio_ffmpeg.get_ffmpeg_exe())"
if (-not (Test-Path $ff)) { throw "ffmpeg not found: $ff" }

& $ff -hide_banner -loglevel error -y -i "$src" -ar 22050 -ac 1 -f s16le -c:a pcm_s16le "$outPcm"
if ($LASTEXITCODE -ne 0) { throw "ffmpeg failed" }

$len = (Get-Item $outPcm).Length
$sec = [math]::Round($len / 44100.0, 1)
Write-Host ("PCM ok: {0:N0} bytes  ~{1} sec" -f $len, $sec)

$limit = ($PartSize * 0.92)
if ($len -gt $limit) {
    $maxSec = [math]::Round($limit / 44100.0, 0)
    Write-Host ("WARNING: file too big for 7MB partition. max about {0} min." -f [math]::Round($maxSec/60,1)) -ForegroundColor Yellow
    $go = Read-Host "continue anyway? (y/n)"
    if ($go -ne "y") { exit }
}

Write-Host ""
Write-Host "=== STEP 3/4 : pack LittleFS image ===" -ForegroundColor Cyan
if (Test-Path $ImgFile) { Remove-Item $ImgFile -Force }
& $Mk -c $DataDir -b 4096 -p 256 -s $PartSize $ImgFile
if ($LASTEXITCODE -ne 0) { throw "mklittlefs failed" }
Write-Host ("image: {0:N0} bytes" -f (Get-Item $ImgFile).Length)
& $Mk -l $ImgFile

Write-Host ""
Write-Host "=== STEP 4/4 : flash to ESP32 ===" -ForegroundColor Cyan
$ports = [System.IO.Ports.SerialPort]::GetPortNames()
if ($ports -contains "COM10") { $port = "COM10" }
elseif ($ports.Count -gt 0) { $port = $ports[0] }
else { throw "no COM port found - is the board plugged in?" }
Write-Host "using port: $port"

& $Esp --chip esp32s3 --port $port --baud 921600 write_flash $FlashAddr $ImgFile
if ($LASTEXITCODE -ne 0) { throw "esptool failed" }

Write-Host ""
Write-Host "=== DONE ===" -ForegroundColor Green
Write-Host "Press RST on the board, then hold BOOT to play."
Write-Host "No re-compile needed."
Write-Host ""
Read-Host "press Enter to close"
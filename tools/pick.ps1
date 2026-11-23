Add-Type -AssemblyName System.Windows.Forms

$dlg = New-Object System.Windows.Forms.OpenFileDialog
$dlg.Filter = "图片文件|*.jpg;*.jpeg;*.png;*.bmp|所有文件|*.*"
$dlg.Title = "选一张图，生成 plantest\image.h"
$dlg.InitialDirectory = "$env:USERPROFILE\Pictures"
$dlg.RestoreDirectory = $true

if ($dlg.ShowDialog() -ne 'OK') {
    Write-Host "没选图，退出。" -ForegroundColor Yellow
    exit 0
}

$src = $dlg.FileName
Write-Host ""
Write-Host "选中: $src" -ForegroundColor Cyan
Write-Host ""

& "$PSScriptRoot\img2h.ps1" -ImagePath $src -OutputPath "D:\zz187\Arduino\experiments\plantest\image.h"
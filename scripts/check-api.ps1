param([Parameter(Mandatory=$true)][string]$BaseUrl, [ValidateRange(1,60)][int]$TimeoutSec=10)
$ErrorActionPreference = 'Stop'
$base = $BaseUrl.TrimEnd('/')
$config = Invoke-RestMethod "$base/api/config" -TimeoutSec $TimeoutSec
$status = Invoke-RestMethod "$base/api/status" -TimeoutSec $TimeoutSec
if ($null -ne $config.wifi_password -or $null -ne $config.mqtt_password) { throw 'Credentials leaked by config endpoint' }
if ($status.state -notin @('unavailable','learning','clear','occupied')) { throw 'Invalid state' }
if ($config.roi.x + $config.roi.width -gt 80 -or $config.roi.y + $config.roi.height -gt 62) { throw 'Invalid ROI' }
if ($status.sensor_ready) {
    $image = Invoke-WebRequest "$base/snapshot.bmp" -UseBasicParsing -TimeoutSec $TimeoutSec
    if ($image.Headers['Content-Type'] -notlike 'image/bmp*') { throw 'Snapshot is not BMP' }
}
$status | Format-List
Write-Output 'PASS: read-only configuration, status and available snapshot checks'

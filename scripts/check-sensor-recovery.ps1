param([string]$BaseUrl='http://192.168.8.179',[string]$Port='COM13',[switch]$SkipRecovery)
$ErrorActionPreference='Stop'
$base=$BaseUrl.TrimEnd('/')
function Status { Invoke-RestMethod "$base/api/status" -TimeoutSec 8 }
function Require($ok,[string]$message) { if(-not $ok) { throw $message } }
function Save-Palette([string]$palette) {
    $body=@{palette=$palette}|ConvertTo-Json -Compress
    $r=Invoke-RestMethod "$base/api/config" -Method Put -Headers @{'X-Thermal-Request'='1'} -ContentType application/json -Body $body -TimeoutSec 8
    Require $r.saved 'Palette save failed'
}
$original=Invoke-RestMethod "$base/api/config" -TimeoutSec 8
Require (-not $original.broker) 'Run this commissioning test with MQTT disabled; it deliberately pauses detection'
$before=Status
Require ($null -ne $before.sensor_config_pauses) 'Firmware lacks capture-pause diagnostics'
Require $before.sensor_ready 'Wait for sensor initialization before testing'
$rows=New-Object System.Collections.Generic.List[object]
$out=Join-Path (Split-Path -Parent $PSScriptRoot) '.cache/sensor-recovery-validation'
New-Item -ItemType Directory -Force $out|Out-Null
try {
    for($n=0;$n -lt 12;$n++) {
        Save-Palette $(if(($n%2)-eq 0){'black_hot'}else{'white_hot'})
        Start-Sleep -Milliseconds 700
        $s=Status;$rows.Add($s)
        Require ($s.sensor_ready -and $s.frame_age_ms -lt 1000) 'Capture failed to resume after saving'
        Require ($s.sensor_errors -eq $before.sensor_errors) 'Settings save caused a sensor error'
        Require ($s.frames -gt $before.frames -and $s.heap_alloc_failures -eq 0) 'Acquisition or allocation failed'
    }
} finally {
    Save-Palette $original.palette
    $rows|ConvertTo-Json -Depth 8|Set-Content (Join-Path $out 'settings.json')
}
Start-Sleep -Seconds 1
$saved=Status
Require ($saved.sensor_config_pauses -eq $before.sensor_config_pauses+13) 'Every NVS save must be protected by a capture pause'
Require ($saved.sensor_errors -eq $before.sensor_errors) 'Restoring the palette caused an error'
$after=Invoke-RestMethod "$base/api/config" -TimeoutSec 8
Require (($original|ConvertTo-Json -Compress -Depth 5) -ceq ($after|ConvertTo-Json -Compress -Depth 5)) 'Configuration was not restored'
'PASS: 13 protected settings saves; palette restored; no receive or allocation errors'
if(-not $SkipRecovery) {
    $reply=(& "$PSScriptRoot/usb-command.ps1" -Port $Port -Command 'SENSOR RECOVER')|ConvertFrom-Json
    Require $reply.accepted 'Manual recovery request rejected'
    $recovered=$false;$sawUnavailable=$false;$rows.Clear()
    for($attempt=0;$attempt -lt 200;$attempt++) {
        Start-Sleep -Milliseconds 100
        $s=Status;$rows.Add($s)
        if($s.state -eq 'unavailable') { $sawUnavailable=$true }
        Require ($s.uptime_ms -ge $saved.uptime_ms) 'Board rebooted during sensor recovery'
        if($s.sensor_ready -and $s.frames -gt $saved.frames -and $s.sensor_recoveries -gt $saved.sensor_recoveries) { $recovered=$true;break }
    }
    $rows|ConvertTo-Json -Depth 8|Set-Content (Join-Path $out 'recovery.json')
    Require $sawUnavailable 'Recovery did not report unavailable'
    Require $recovered 'Sensor did not recover during the polling window'
    Require ($s.sensor_recoveries -eq $saved.sensor_recoveries+1 -and $s.sensor_errors -eq $saved.sensor_errors) 'Recovery retried or caused additional receive errors'
    Require ($s.state -eq 'learning' -and $s.small.state -eq 'learning' -and $s.large.state -eq 'learning' -and $s.sensor_warmup_ms -gt 0) 'Recovery must restart warm-up for all detection channels'
    'PASS: sensor-only recovery; unavailable then learning on all channels; no board reboot'
}

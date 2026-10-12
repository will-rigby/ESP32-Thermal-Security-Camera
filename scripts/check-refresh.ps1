param([Parameter(Mandatory=$true)][string]$BaseUrl)
# Commissioning check: temporarily changes the refresh rate and restarts
# learning on each save, then restores the original rate even on failure.
$ErrorActionPreference='Stop'
$base=$BaseUrl.TrimEnd('/')
$original=Invoke-RestMethod "$base/api/config" -TimeoutSec 8
if($null -eq $original.video_target_fps){throw 'Refresh-rate firmware is required'}
$headers=@{'X-Thermal-Request'='1'}
function Set-Rate([double]$rate) {
    $body=@{video_target_fps=$rate}|ConvertTo-Json -Compress
    Invoke-RestMethod "$base/api/config" -Method Put -ContentType 'application/json' -Headers $headers -Body $body -TimeoutSec 8 | Out-Null
    Start-Sleep -Milliseconds 500
}
try {
    foreach($invalid in @('0','21','3','0.3','null','"5"','true')) {
        $rejected=$false
        try {Invoke-RestMethod "$base/api/config" -Method Put -ContentType 'application/json' -Headers $headers -Body ('{"video_target_fps":'+$invalid+'}') -TimeoutSec 8|Out-Null}
        catch {if([int]$_.Exception.Response.StatusCode -eq 400){$rejected=$true}else{throw}}
        if(-not $rejected){throw "Accepted invalid refresh rate: $invalid"}
    }
    foreach($rate in @(20,10,5,2,1,0.5,0.2,0.1)) {
        Set-Rate $rate
        $before=Invoke-RestMethod "$base/api/status" -TimeoutSec 5
        if($before.video_target_fps -ne $rate){throw 'Saved rate not reflected in status'}
        $seconds=[int][Math]::Max(3,[Math]::Ceiling(2/$rate+1))
        & (Join-Path $PSScriptRoot 'check-video.ps1') -BaseUrl $base -Seconds $seconds -MinimumFps ($rate*0.75)
        $after=Invoke-RestMethod "$base/api/status" -TimeoutSec 5
        if(-not $after.sensor_ready -or $after.frames-$before.frames -lt $seconds*5){throw 'Acquisition slowed unexpectedly'}
        if($after.heap_alloc_failures -ne $before.heap_alloc_failures -or $after.sensor_errors -ne $before.sensor_errors){throw 'New sensor/allocation error'}
        if($after.video_fps -lt $rate*0.7 -or $after.video_fps -gt $rate*1.1){throw 'Reported unique-image FPS outside expected range'}
        $snapshot=Invoke-WebRequest "$base/snapshot.bmp" -UseBasicParsing -TimeoutSec 5
        if($snapshot.Headers['Content-Type'] -notlike 'image/bmp*'){throw 'Held snapshot unavailable'}
        [pscustomobject]@{TargetFPS=$rate;ReportedFPS=$after.video_fps;SensorFPS=$after.fps;ChipC=$after.esp32_temperature_c;InternalHeap=$after.free_internal_heap;MinimumInternalHeap=$after.min_internal_heap}
    }
} finally {
    Set-Rate $original.video_target_fps
    $restored=Invoke-RestMethod "$base/api/config" -TimeoutSec 8
    if(($restored|ConvertTo-Json -Depth 8 -Compress) -cne ($original|ConvertTo-Json -Depth 8 -Compress)){throw 'Configuration was not fully restored'}
}
Write-Output 'PASS: all saved rates, invalid values, unique-image delivery, held snapshots and configuration restoration'

param([Parameter(Mandatory=$true)][string]$BaseUrl)
$ErrorActionPreference='Stop'
$base=$BaseUrl.TrimEnd('/')
$headers=@{'X-Thermal-Request'='1'}
$original=Invoke-RestMethod "$base/api/config" -TimeoutSec 5
if($original.tone_mapping -notin @('adaptive','linear')){throw 'Contrast setting missing'}
function Save-Mode([string]$mode) {
    Invoke-RestMethod "$base/api/config" -Method Put -Headers $headers -ContentType 'application/json' -Body (@{tone_mapping=$mode}|ConvertTo-Json -Compress) -TimeoutSec 10 | Out-Null
    $deadline=[DateTime]::UtcNow.AddSeconds(15)
    do {
        Start-Sleep -Milliseconds 250
        $health=Invoke-RestMethod "$base/api/status" -TimeoutSec 5
        if($health.sensor_ready -and $health.tone_mapping -eq $mode -and $health.video_fps -gt 0){return $health}
    } while([DateTime]::UtcNow -lt $deadline)
    throw "No healthy video after saving $mode"
}
try {
    foreach($mode in @('linear','adaptive')) {
        $health=Save-Mode $mode
        $saved=Invoke-RestMethod "$base/api/config" -TimeoutSec 5
        if($saved.tone_mapping -ne $mode){throw 'Mode did not save'}
        foreach($field in @('palette','flip_horizontal','video_target_fps','ssid','broker','delta_c','min_pixels','large_min_pixels')) {
            if($saved.$field -ne $original.$field){throw "Unrelated setting changed: $field"}
        }
        $image=Invoke-WebRequest "$base/snapshot.bmp" -UseBasicParsing -TimeoutSec 5
        if($image.Headers['Content-Type'] -notlike 'image/bmp*' -or $image.RawContentLength -ne 6038){throw 'Invalid BMP snapshot'}
        [pscustomobject]@{Mode=$mode;SensorFPS=$health.fps;VideoFPS=$health.video_fps;RenderUs=$health.render_us;SensorErrors=$health.sensor_errors;AllocationFailures=$health.heap_alloc_failures}
    }
    foreach($body in @('{"tone_mapping":"log"}','{"tone_mapping":null}','{"tone_mapping":1}')) {
        $rejected=$false
        try { Invoke-RestMethod "$base/api/config" -Method Put -Headers $headers -ContentType 'application/json' -Body $body -TimeoutSec 5 | Out-Null }
        catch { if($_.Exception.Response.StatusCode.value__ -ne 400){throw};$rejected=$true }
        if(-not $rejected){throw "Invalid contrast setting accepted: $body"}
        if((Invoke-RestMethod "$base/api/config" -TimeoutSec 5).tone_mapping -ne 'adaptive'){throw 'Rejected save altered mode'}
    }
    $page=(Invoke-WebRequest "$base/settings" -UseBasicParsing -TimeoutSec 5).Content
    if($page -notmatch 'id="tone_mapping"' -or $page -notmatch 'Adaptive detail'){throw 'Settings control missing'}
    Write-Output 'PASS: both contrast modes, partial-save preservation, snapshots, invalid-input rejection and settings control'
} finally {
    if((Invoke-RestMethod "$base/api/config" -TimeoutSec 5).tone_mapping -ne $original.tone_mapping){Save-Mode $original.tone_mapping | Out-Null}
}

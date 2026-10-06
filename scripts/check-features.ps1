param([string]$BaseUrl='http://192.168.8.179',[string]$Port='COM13')
$ErrorActionPreference='Stop'
$base=$BaseUrl.TrimEnd('/')
function Get-Config { Invoke-RestMethod "$base/api/config" -TimeoutSec 8 }
function Save-Config($value) {
    $body=$value | ConvertTo-Json -Compress -Depth 5
    $reply=Invoke-RestMethod "$base/api/config" -Method Put -Headers @{'X-Thermal-Request'='1'} -ContentType application/json -Body $body -TimeoutSec 8
    if(-not $reply.saved) { throw 'Settings were not saved' }
}
function Require($value,[string]$message) { if(-not $value) { throw $message } }
$original=Get-Config
Require (-not $original.broker) 'This commissioning test temporarily toggles discovery; run with MQTT disabled to avoid changing live Home Assistant entities'
$root=Invoke-WebRequest "$base/" -UseBasicParsing -TimeoutSec 8
$settings=Invoke-WebRequest "$base/settings" -UseBasicParsing -TimeoutSec 8
Require ($root.Content -notmatch '<form' -and $root.Content -match 'href="/settings"') 'Main viewer route is incorrect'
Require ($settings.Content -match 'id="palette"' -and $settings.Content -match 'id="large_min_pixels"' -and $settings.Content -match 'id="ha_discovery"') 'Settings page lacks new controls'
Add-Type -AssemblyName System.Drawing
$out=Join-Path (Split-Path -Parent $PSScriptRoot) '.cache/feature-validation'
New-Item -ItemType Directory -Force $out | Out-Null
try {
    foreach($bad in @(@{palette='invalid'},@{palette=42},@{ha_discovery='true'},@{large_min_pixels=0},@{large_min_pixels=4961},@{large_min_pixels=4.5})) {
        $rejected=$false
        try { Save-Config $bad } catch {
            if($_.Exception.Response -and [int]$_.Exception.Response.StatusCode -eq 400) { $rejected=$true } else { throw }
        }
        Require $rejected 'Invalid configuration was accepted'
    }
    foreach($palette in @('fire','ironbow','rainbow','white_hot','black_hot')) {
        Save-Config @{palette=$palette}
        # Settings writes and acquisition/rendering are asynchronous. Allow a
        # fresh YUY2 frame to pass through both tasks, and bypass host caching.
        Start-Sleep -Seconds 2
        $status=Invoke-RestMethod "$base/api/status" -TimeoutSec 8
        Require ($status.palette -eq $palette -and $status.video_enabled -and $status.heap_alloc_failures -eq 0) 'Palette application or memory health failed'
        $file=Join-Path $out "$palette.bmp"
        Invoke-WebRequest ("$base/snapshot.bmp?t="+[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()) -OutFile $file -TimeoutSec 8
        $bitmap=[System.Drawing.Bitmap]::FromFile($file)
        try {
            Require ($bitmap.Width -eq 80 -and $bitmap.Height -eq 62) 'Snapshot dimensions changed'
            # Colour palettes are browser-side; every device snapshot is grayscale.
            $colourful=0
            for($y=0;$y -lt 62;$y++) { for($x=0;$x -lt 80;$x++) {
                $pixel=$bitmap.GetPixel($x,$y)
                if($pixel.R -ne $pixel.G -or $pixel.B -ne $pixel.G) { $colourful++ }
            } }
            Require ($colourful -eq 0) "Grayscale snapshot produced unexpected colour: $palette"
        } finally { $bitmap.Dispose() }
    }
    Save-Config @{palette='ironbow';large_min_pixels=24;ha_discovery=$true}
    $messages=(& "$PSScriptRoot/usb-command.ps1" -Port $Port -Command DISCOVERY -TimeoutSec 10) | ConvertFrom-Json
    Require ($messages.Count -eq 3) 'Expected three discovered occupancy sensors'
    foreach($message in $messages) {
        $p=$message.payload
        Require ($message.topic -like 'homeassistant/binary_sensor/thermal_*/config') 'Invalid discovery topic'
        Require ($p.device_class -eq 'occupancy' -and $p.qos -eq 1) 'Invalid discovery sensor type/QoS'
        Require ($p.availability_mode -eq 'all' -and $p.availability.Count -eq 2) 'Learning/fault availability missing'
        Require ($p.payload_on -eq 'occupied' -and $p.payload_off -eq 'clear') 'Incorrect occupancy mapping'
        Require ($p.state_topic.StartsWith($original.topic+'/')) 'Discovery must follow configured topic prefix'
    }
    Require (($messages.payload.unique_id | Select-Object -Unique).Count -eq 3) 'Discovery IDs collide'
    $messages | ConvertTo-Json -Depth 10 | Set-Content (Join-Path $out 'discovery.json')
    Save-Config @{ha_discovery=$false}
    $removed=(& "$PSScriptRoot/usb-command.ps1" -Port $Port -Command DISCOVERY -TimeoutSec 10) | ConvertFrom-Json
    foreach($message in $removed) { Require ($message.payload -ceq '') 'Disabled discovery must publish retained tombstones' }
    Save-Config @{ha_discovery=$true}
    & "$PSScriptRoot/usb-command.ps1" -Port $Port -Command REBOOT | Out-Null
    $reconnected=$false
    for($attempt=0;$attempt -lt 20;$attempt++) {
        Start-Sleep -Seconds 2
        try { $saved=Get-Config; $health=Invoke-RestMethod "$base/api/status" -TimeoutSec 2; if($health.uptime_ms -lt 60000) { $reconnected=$true;break } } catch { }
    }
    Require $reconnected 'Device did not reconnect after restart'
    Require ($saved.palette -eq 'ironbow' -and $saved.large_min_pixels -eq 24 -and $saved.ha_discovery) 'New settings did not survive restart'
    Require ($saved.ssid -eq $original.ssid -and $saved.has_wifi_password -eq $original.has_wifi_password -and $saved.flip_horizontal -eq $original.flip_horizontal) 'Existing settings changed'
} finally {
    Save-Config @{palette=$original.palette;large_min_pixels=$original.large_min_pixels;ha_discovery=$original.ha_discovery}
}
for($attempt=0;$attempt -lt 15;$attempt++) {
    $final=Invoke-RestMethod "$base/api/status" -TimeoutSec 8
    if($final.sensor_ready -and $final.video_frames -gt 0) { break }
    Start-Sleep -Seconds 2
}
$final | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $out 'status.json')
Require ($final.sensor_ready -and $final.video_frames -gt 0 -and $final.heap_alloc_failures -eq 0 -and $final.sensor_errors -eq 0) 'Final hardware health check failed'
Write-Output 'PASS: viewer/settings routes, strict config validation, five saved palettes, grayscale BMPs, discovery payloads/removal, saved-setting migration and reboot persistence. Browser palette rendering and live MQTT/HA remain separate checks.'

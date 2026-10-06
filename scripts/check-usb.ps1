param(
    [string]$Ffmpeg = 'ffmpeg.exe',
    [string]$Ffprobe = 'ffprobe.exe',
    [string]$Device = 'Thermal camera',
    [ValidateRange(1, 100)][int]$Attempts = 3,
    [ValidateRange(1, 300)][int]$Seconds = 5,
    [ValidateRange(0.1, 20)][double]$MinimumFps = 18
)
$ErrorActionPreference = 'Stop'
$captureDirectory = Join-Path (Split-Path -Parent $PSScriptRoot) '.cache/usb-validation'
New-Item -ItemType Directory -Force $captureDirectory | Out-Null
$ffmpegExecutable = (Get-Command $Ffmpeg -CommandType Application -ErrorAction Stop).Source
$ffprobeExecutable = (Get-Command $Ffprobe -CommandType Application -ErrorAction Stop).Source
if ($Device.Contains('"')) { throw 'Camera name must not contain a double quote' }
foreach ($attempt in 1..$Attempts) {
    $captureFile = Join-Path $captureDirectory "capture-$attempt.nut"
    $errorFile = Join-Path $captureDirectory "capture-$attempt.log"
    $outputFile = Join-Path $captureDirectory "capture-$attempt.stdout.log"
    $captureArguments = @('-hide_banner', '-nostdin', '-y', '-f', 'dshow',
        '-video_size', '80x62', '-framerate', '20', '-pixel_format', 'yuyv422',
        '-use_wallclock_as_timestamps', '1',
        '-i', ('video="' + $Device + '"'), '-t', $Seconds, '-an', '-c:v', 'copy',
        ('"' + $captureFile + '"'))
    $captureClock = [System.Diagnostics.Stopwatch]::StartNew()
    $capture = Start-Process -FilePath $ffmpegExecutable -ArgumentList $captureArguments `
        -RedirectStandardError $errorFile -RedirectStandardOutput $outputFile `
        -PassThru -WindowStyle Hidden
    try {
        # Retain the process handle before it exits so Windows PowerShell can
        # retrieve ExitCode reliably, including very short successful captures.
        $processHandle = $capture.Handle
        if (-not $capture.WaitForExit(($Seconds + 15) * 1000)) {
            throw "Camera open $attempt timed out; see $errorFile"
        }
        $capture.WaitForExit()
        if ($capture.ExitCode -ne 0) { throw "Camera open $attempt failed; see $errorFile" }
    } finally {
        if (-not $capture.HasExited) { $capture.Kill(); $capture.WaitForExit() }
        $capture.Dispose()
        $captureClock.Stop()
    }
    $probeText = & $ffprobeExecutable -v error -count_frames `
        -show_entries stream=codec_name,pix_fmt,width,height,avg_frame_rate,nb_read_frames `
        -show_entries format=duration -of json $captureFile
    if ($LASTEXITCODE -ne 0) { throw "Capture $attempt cannot be decoded" }
    $probe = $probeText | ConvertFrom-Json
    $stream = $probe.streams[0]
    if ($stream.codec_name -ne 'rawvideo' -or $stream.pix_fmt -ne 'yuyv422' -or $stream.width -ne 80 -or $stream.height -ne 62) {
        throw "Capture $attempt has an unexpected format"
    }
    $duration = [double]::Parse($probe.format.duration, [System.Globalization.CultureInfo]::InvariantCulture)
    if ($duration -le 0) { throw "Capture $attempt has no valid duration" }
    # UVC's nominal 20 FPS timestamps can conceal slow delivery. The input
    # uses host wall-clock timestamps so the file duration reflects arrivals.
    $measuredFps = [int]$stream.nb_read_frames / $duration
    [pscustomobject]@{
        Attempt = $attempt
        Frames = [int]$stream.nb_read_frames
        CapturedSeconds = $duration
        ElapsedSeconds = [Math]::Round($captureClock.Elapsed.TotalSeconds, 3)
        MeasuredFPS = [Math]::Round($measuredFps, 2)
        AdvertisedFPS = $stream.avg_frame_rate
        File = $captureFile
    }
    if ($measuredFps -lt $MinimumFps -or [int]$stream.nb_read_frames -lt [Math]::Floor($Seconds * $MinimumFps)) {
        throw "Capture $attempt delivered fewer than $MinimumFps frames per second"
    }
}
Write-Output "PASS: $Attempts DirectShow capture(s) at least $MinimumFps FPS; Windows Camera and OBS still need separate checks"

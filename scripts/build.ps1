param(
    [string]$Cli = 'arduino-cli',
    [string]$ConfigFile = '',
    [switch]$TestPattern,
    [switch]$NoUsb
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    $lock = Get-Content (Join-Path $root 'dependencies.lock.json') -Raw | ConvertFrom-Json
    $sensorHash = (Get-FileHash (Join-Path $root 'libraries/WaveshareSenxor/src/esp32s3/libsenxorLib4M.a') -Algorithm SHA256).Hash
    if ($sensorHash -ne $lock.sensor.sha256) { throw 'Sensor archive differs from the pinned vendor binary' }
    $options = 'USBMode=default,CDCOnBoot=default,FlashSize=16M,FlashMode=qio,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB'
    $argsList = @('compile','--fqbn',"esp32:esp32:esp32s3:$options",'--libraries',(Join-Path $root 'libraries'))
    $mode = if ($TestPattern) { 'test-pattern' } elseif ($NoUsb) { 'network-only' } else { 'camera' }
    $argsList += @('--build-path',(Join-Path $root ".cache/build/$mode"),'--output-dir',(Join-Path $root "dist/$mode"))
    if ($ConfigFile) { $argsList += @('--config-file',$ConfigFile) }
    $defines = @()
    if ($TestPattern) { $defines += '-DTHERMAL_TEST_PATTERN=1' }
    if ($NoUsb) { $defines += '-DTHERMAL_ENABLE_USB=0' }
    if ($defines.Count) { $argsList += @('--build-property',('compiler.cpp.extra_flags=' + ($defines -join ' '))) }
    $argsList += 'firmware/ThermalSecurityCamera'
    & $Cli @argsList
    if ($LASTEXITCODE -ne 0) { throw 'Arduino compilation failed' }
} finally { Pop-Location }

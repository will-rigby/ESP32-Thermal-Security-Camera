param([string]$Cli = 'arduino-cli')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$localRoot = ($root -replace '\\','/')
$config = Join-Path $root 'arduino-cli.local.yaml'
@"
board_manager:
  additional_urls:
    - https://espressif.github.io/arduino-esp32/package_esp32_index.json
directories:
  data: $localRoot/.cache/arduino/data
  downloads: $localRoot/.cache/arduino/downloads
  user: $localRoot/.cache/arduino/user
"@ | Set-Content -LiteralPath $config -Encoding UTF8
& $Cli core update-index --config-file $config
if ($LASTEXITCODE -ne 0) { throw 'Arduino index update failed' }
& $Cli core install esp32:esp32@3.3.12 --config-file $config
if ($LASTEXITCODE -ne 0) { throw 'Pinned Arduino core installation failed' }

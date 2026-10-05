param([string]$Compiler = 'g++')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    New-Item -ItemType Directory -Force '.cache/tests' | Out-Null
    & $Compiler -std=c++17 -O2 -Wall -Wextra -Werror tests/detector_tests.cpp firmware/ThermalSecurityCamera/src/Detector.cpp -o .cache/tests/detector_tests.exe
    if ($LASTEXITCODE -ne 0) { throw 'Native test compilation failed' }
    & '.cache/tests/detector_tests.exe'
    if ($LASTEXITCODE -ne 0) { throw 'Detector tests failed' }
} finally { Pop-Location }

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
    & $Compiler -std=c++17 -O2 -Wall -Wextra -Werror tests/yuy2_tests.cpp -o .cache/tests/yuy2_tests.exe
    if ($LASTEXITCODE -ne 0) { throw 'YUY2 test compilation failed' }
    & '.cache/tests/yuy2_tests.exe' '.cache/tests/gray-frame.yuy2'
    if ($LASTEXITCODE -ne 0) { throw 'YUY2 tests failed' }
    & $Compiler -std=c++17 -O2 -Wall -Wextra -Werror tests/video_timing_tests.cpp -o .cache/tests/video_timing_tests.exe
    if ($LASTEXITCODE -ne 0) { throw 'Video timing test compilation failed' }
    & '.cache/tests/video_timing_tests.exe'
    if ($LASTEXITCODE -ne 0) { throw 'Video timing tests failed' }
    & $Compiler -std=c++17 -O2 -Wall -Wextra -Werror tests/tone_mapping_tests.cpp -o .cache/tests/tone_mapping_tests.exe
    if ($LASTEXITCODE -ne 0) { throw 'Tone mapping test compilation failed' }
    & '.cache/tests/tone_mapping_tests.exe'
    if ($LASTEXITCODE -ne 0) { throw 'Tone mapping tests failed' }
} finally { Pop-Location }

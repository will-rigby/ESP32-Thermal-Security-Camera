param([string]$Compiler='g++')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    $dependency='.cache/tests/cjson'
    New-Item -ItemType Directory -Force $dependency | Out-Null
    # Test-only upstream cJSON 1.7.19; source stays in the ignored cache.
    $hashes=@{
        'cJSON.c'='298581A04A36C0165DA4B0AADE235C23088CB2FAA58651D720EA2F3706ED0B0D'
        'cJSON.h'='25B0145150D500498E4D209CEC69C18C42CF818BFFCC54690BE3B895A2A16DEE'
    }
    foreach($name in $hashes.Keys) {
        $file=Join-Path $dependency $name
        if(-not (Test-Path -LiteralPath $file)) {
            Invoke-WebRequest -UseBasicParsing "https://raw.githubusercontent.com/DaveGamble/cJSON/v1.7.19/$name" -OutFile $file
        }
        if((Get-FileHash $file -Algorithm SHA256).Hash -ne $hashes[$name]) { throw "Unexpected cJSON dependency hash: $name" }
    }
    & $Compiler -std=c++17 -O2 -Wall -Wextra -Werror -Itests/host "-I$dependency" -x c++ tests/fire_integration_tests.cpp firmware/ThermalSecurityCamera/src/Config.cpp firmware/ThermalSecurityCamera/src/Detector.cpp firmware/ThermalSecurityCamera/src/FireJson.cpp firmware/ThermalSecurityCamera/src/HaDiscovery.cpp "$dependency/cJSON.c" -o .cache/tests/fire_integration_tests.exe
    if($LASTEXITCODE -ne 0) { throw 'Fire integration test compilation failed' }
    & '.cache/tests/fire_integration_tests.exe'
    if($LASTEXITCODE -ne 0) { throw 'Fire integration tests failed' }
    foreach($synthetic in @(0,1)) {
        $output=".cache/tests/fire_network_$synthetic.exe"
        & $Compiler -std=c++17 -O2 -Wall -Wextra -Werror -Itests/host "-I$dependency" "-DTHERMAL_TEST_PATTERN=$synthetic" -x c++ tests/fire_network_tests.cpp firmware/ThermalSecurityCamera/src/Config.cpp firmware/ThermalSecurityCamera/src/Detector.cpp firmware/ThermalSecurityCamera/src/FireJson.cpp firmware/ThermalSecurityCamera/src/HaDiscovery.cpp "$dependency/cJSON.c" -o $output
        if($LASTEXITCODE -ne 0) { throw 'Fire network test compilation failed' }
        & $output
        if($LASTEXITCODE -ne 0) { throw 'Fire network tests failed' }
    }
} finally { Pop-Location }

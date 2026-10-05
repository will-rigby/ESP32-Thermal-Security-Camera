$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$output = Join-Path $root 'dist'
New-Item -ItemType Directory -Force -Path $output | Out-Null
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$library = Join-Path $root 'libraries/WaveshareSenxor'
$version = ((Get-Content (Join-Path $library 'library.properties') | Where-Object { $_ -match '^version=' }) -replace '^version=','').Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw 'Invalid library version' }
$stream = [System.IO.File]::Open((Join-Path $output "WaveshareSenxor-$version.zip"),[System.IO.FileMode]::Create)
$archive = New-Object System.IO.Compression.ZipArchive($stream,[System.IO.Compression.ZipArchiveMode]::Create)
try {
    Get-ChildItem -LiteralPath $library -Recurse -File | ForEach-Object {
        $entry = 'WaveshareSenxor/' + ($_.FullName.Substring($library.Length + 1) -replace '\\','/')
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive,$_.FullName,$entry) | Out-Null
    }
} finally { $archive.Dispose(); $stream.Dispose() }

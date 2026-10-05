param(
    [Parameter(Mandatory=$true)][string]$Port,
    [string]$Command = 'STATUS',
    [ValidateRange(1,30)][int]$TimeoutSec = 5
)
$ErrorActionPreference = 'Stop'
if ($Command.Contains("`n") -or $Command.Contains("`r")) { throw 'Send one command per call' }
$serial = New-Object System.IO.Ports.SerialPort($Port, 115200, 'None', 8, 'One')
$serial.NewLine = "`n"
$serial.ReadTimeout = $TimeoutSec * 1000
$serial.WriteTimeout = $TimeoutSec * 1000
try {
    $serial.Open()
    $serial.DtrEnable = $false
    $serial.RtsEnable = $false
    Start-Sleep -Milliseconds 100
    $serial.DtrEnable = $true
    Start-Sleep -Milliseconds 100
    $serial.RtsEnable = $true
    Start-Sleep -Milliseconds 200
    $serial.DiscardInBuffer()
    $serial.WriteLine($Command)
    $serial.ReadLine().Trim()
} finally {
    if ($serial.IsOpen) { $serial.Close() }
    $serial.Dispose()
}

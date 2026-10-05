param(
    [Parameter(Mandatory=$true)][string]$BaseUrl,
    [int]$Hours = 24,
    [string]$OutputFile = 'thermal-soak.jsonl'
)
$ErrorActionPreference = 'Stop'
$end = [DateTime]::UtcNow.AddHours($Hours)
$base = $BaseUrl.TrimEnd('/')
while ([DateTime]::UtcNow -lt $end) {
    $record = @{ observed_utc = [DateTime]::UtcNow.ToString('o') }
    try { $record.status = Invoke-RestMethod "$base/api/status" -TimeoutSec 10 }
    catch { $record.error = $_.Exception.Message }
    $record | ConvertTo-Json -Depth 8 -Compress | Add-Content -LiteralPath $OutputFile -Encoding UTF8
    Start-Sleep -Seconds 60
}

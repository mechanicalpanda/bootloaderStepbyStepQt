$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$sources = @(
    'src\WinUsbDeviceDiscovery.cpp',
    'src\WinUsbTransport.cpp'
)
$pattern = 'CreateFileW\s*\([\s\S]*?OPEN_EXISTING,\s*FILE_FLAG_OVERLAPPED,\s*nullptr\s*\)'

foreach ($relativePath in $sources)
{
    $path = Join-Path $root $relativePath
    $text = Get-Content -Raw -LiteralPath $path
    if ($text -notmatch $pattern)
    {
        throw "$relativePath must open the WinUSB device with FILE_FLAG_OVERLAPPED"
    }
}

Write-Host 'PASS: all WinUSB device handles use FILE_FLAG_OVERLAPPED'

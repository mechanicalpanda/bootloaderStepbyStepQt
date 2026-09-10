param(
    [string]$AppRoot = 'I:\MYFOC\YuanziHardwareWithMyFirmware\FOCTEST',
    [string]$BootloaderRoot = 'I:\MYFOC\bootloader\bootloaderStepbyStep'
)

$ErrorActionPreference = 'Stop'
$profilePath = Join-Path $AppRoot 'config\firmware_profile.json'
$identityPath = Join-Path $BootloaderRoot 'Bootloader\Inc\bl_device_identity.h'
$profile = Get-Content -LiteralPath $profilePath -Raw | ConvertFrom-Json
$identity = Get-Content -LiteralPath $identityPath -Raw

$productMatch = [regex]::Match(
    $identity,
    'BL_DEVICE_PRODUCT_ID\s+UINT32_C\(0x([0-9A-Fa-f]{8})\)')
$hardwareMatch = [regex]::Match(
    $identity,
    'BL_DEVICE_HARDWARE_REVISION\s+UINT16_C\(([0-9]+)\)')
if (-not $productMatch.Success -or -not $hardwareMatch.Success) {
    throw 'Bootloader device identity constants could not be parsed.'
}

$appProduct = [Convert]::ToUInt32(
    $profile.product_id.ToString().Substring(2), 16)
$bootProduct = [Convert]::ToUInt32($productMatch.Groups[1].Value, 16)
$appHardware = [uint16]$profile.hardware_revision
$bootHardware = [uint16]$hardwareMatch.Groups[1].Value

if ($appProduct -ne $bootProduct) {
    throw ('Product ID drift: APP=0x{0:X8}, Bootloader=0x{1:X8}' -f `
        $appProduct, $bootProduct)
}
if ($appHardware -ne $bootHardware) {
    throw ('Hardware revision drift: APP={0}, Bootloader={1}' -f `
        $appHardware, $bootHardware)
}
if ($appHardware -lt [uint16]$profile.hw_rev_min `
    -or $appHardware -gt [uint16]$profile.hw_rev_max) {
    throw 'APP hardware revision is outside its declared compatibility range.'
}

Write-Output ('PASS: product_id=0x{0:X8}, hardware_revision={1}, range={2}..{3}' -f `
    $appProduct, $appHardware, $profile.hw_rev_min, $profile.hw_rev_max)

# NovaDroid - setup-dev-environment.ps1 (TZ 12, Stage 0)
# Checks virtualization, enables Windows Hypervisor Platform, prepares folders.
# Run from an elevated PowerShell: powershell -ExecutionPolicy Bypass -File setup-dev-environment.ps1

$ErrorActionPreference = "Continue"
Write-Host "=== NovaDroid dev environment setup ===" -ForegroundColor Cyan

# 1. CPU virtualization (VT-x / AMD-V) in firmware
$virt = $false
try {
    $cpu = Get-CimInstance Win32_Processor
    $virt = [bool]($cpu.VirtualizationFirmwareEnabled)
    if (-not $virt) {
        # may be reported as enabled but hidden by running hypervisor
        $virt = [bool](Get-CimInstance Win32_ComputerSystem).HypervisorPresent
    }
} catch {}
if ($virt) {
    Write-Host "[OK]   CPU virtualization (VT-x/AMD-V) detected" -ForegroundColor Green
} else {
    Write-Host "[FAIL] CPU virtualization not detected. Enable Intel VT-x / AMD SVM in BIOS/UEFI." -ForegroundColor Red
}

# 2. Windows Hypervisor Platform feature
$whpx = Get-WindowsOptionalFeature -Online -FeatureName HypervisorPlatform 2>$null
if ($whpx -and $whpx.State -eq "Enabled") {
    Write-Host "[OK]   Windows Hypervisor Platform enabled" -ForegroundColor Green
} else {
    Write-Host "[..]   Enabling Windows Hypervisor Platform..." -ForegroundColor Yellow
    try {
        Enable-WindowsOptionalFeature -Online -FeatureName HypervisorPlatform -All -NoRestart | Out-Null
        Write-Host "[OK]   Enabled. REBOOT REQUIRED." -ForegroundColor Green
    } catch {
        Write-Host "[FAIL] Could not enable feature: $($_.Exception.Message)" -ForegroundColor Red
    }
}

# 3. Hyper-V management (optional, informational)
if (Get-Service vmms -ErrorAction SilentlyContinue) {
    Write-Host "[INFO] Hyper-V management service installed" 
} else {
    Write-Host "[INFO] Hyper-V not installed (not required; WHPX is enough)"
}

# 4. Folder layout (TZ 8.1)
$root = Join-Path $env:LOCALAPPDATA "NovaDroid"
foreach ($d in "instances","images","backups","logs","cache","database","apks","screenshots","configs") {
    New-Item -ItemType Directory -Force -Path (Join-Path $root $d) | Out-Null
}
New-Item -ItemType Directory -Force -Path (Join-Path $PSScriptRoot "..\app\qemu") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $PSScriptRoot "..\app\adb")  | Out-Null
Write-Host "[OK]   Data root ready: $root"

# 5. Component presence hints
$q = Join-Path $PSScriptRoot "..\app\qemu\qemu-system-x86_64.exe"
$a = Join-Path $PSScriptRoot "..\app\adb\adb.exe"
if (Test-Path $q) { Write-Host "[OK]   qemu-system-x86_64.exe found" -ForegroundColor Green }
else { Write-Host "[WARN] qemu-system-x86_64.exe NOT found in app\qemu - download QEMU for Windows" -ForegroundColor Yellow }
if (Test-Path $a) { Write-Host "[OK]   adb.exe found" -ForegroundColor Green }
else { Write-Host "[WARN] adb.exe NOT found in app\adb - download Android SDK Platform Tools" -ForegroundColor Yellow }

Write-Host "=== Done. Reboot if features were changed. ===" -ForegroundColor Cyan

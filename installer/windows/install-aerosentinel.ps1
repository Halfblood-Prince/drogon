[CmdletBinding()]
param(
    [switch]$AllUsers,
    [switch]$InstallWebView2Runtime,
    [switch]$NoLaunch,
    [string]$InstallDir
)

$ErrorActionPreference = "Stop"

function Test-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-TargetArchitecture {
    $architecture = [Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
    switch ($architecture) {
        "Arm64" { return "arm64" }
        "X64" { return "x64" }
        default { throw "Unsupported Windows architecture: $architecture. AeroSentinel ships Windows builds for x64 and arm64." }
    }
}

function Test-WebView2Runtime {
    $clientId = "{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}"
    $keys = @(
        "HKLM:\SOFTWARE\Microsoft\EdgeUpdate\Clients\$clientId",
        "HKLM:\SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\$clientId",
        "HKCU:\SOFTWARE\Microsoft\EdgeUpdate\Clients\$clientId"
    )

    foreach ($key in $keys) {
        if (Test-Path $key) {
            return $true
        }
    }

    return $false
}

function Install-WebView2Runtime {
    if (Test-WebView2Runtime) {
        return
    }

    Write-Host "Installing Microsoft Edge WebView2 Runtime..."
    $bootstrapper = Join-Path $env:TEMP "MicrosoftEdgeWebView2Setup.exe"
    Invoke-WebRequest -UseBasicParsing -Uri "https://go.microsoft.com/fwlink/p/?LinkId=2124703" -OutFile $bootstrapper
    Start-Process -FilePath $bootstrapper -ArgumentList "/silent", "/install" -Wait
}

function Escape-SingleQuotedString([string]$Value) {
    return $Value -replace "'", "''"
}

$scriptRoot = $PSScriptRoot
$targetArchitecture = Get-TargetArchitecture
$payloadRoot = Join-Path $scriptRoot "payload\$targetArchitecture"

if (!(Test-Path $payloadRoot) -and $targetArchitecture -eq "arm64") {
    Write-Warning "No arm64 payload was found. Falling back to x64 under Windows emulation."
    $targetArchitecture = "x64"
    $payloadRoot = Join-Path $scriptRoot "payload\x64"
}

if (!(Test-Path $payloadRoot)) {
    throw "Installer payload not found: $payloadRoot"
}

$isAdmin = Test-Administrator
if ($AllUsers -and !$isAdmin) {
    throw "The -AllUsers option requires an elevated PowerShell session."
}

$perMachine = $AllUsers -or $isAdmin
if ([string]::IsNullOrWhiteSpace($InstallDir)) {
    if ($perMachine) {
        $InstallDir = Join-Path $env:ProgramFiles "AeroSentinel Control Center"
    } else {
        $InstallDir = Join-Path $env:LOCALAPPDATA "Programs\AeroSentinel Control Center"
    }
}

if ($InstallWebView2Runtime) {
    Install-WebView2Runtime
} elseif (!(Test-WebView2Runtime)) {
    Write-Warning "Microsoft Edge WebView2 Runtime was not detected. Install it or rerun this installer with -InstallWebView2Runtime."
}

Write-Host "Installing AeroSentinel Control Center ($targetArchitecture) to $InstallDir"
Stop-Process -Name "aerosentinel-desktop", "aerosentinel-control" -ErrorAction SilentlyContinue

New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
Copy-Item -Path (Join-Path $payloadRoot "*") -Destination $InstallDir -Recurse -Force

$programsFolder = if ($perMachine) {
    [Environment]::GetFolderPath("CommonPrograms")
} else {
    [Environment]::GetFolderPath("Programs")
}

$shortcutFolder = Join-Path $programsFolder "AeroSentinel"
$shortcutPath = Join-Path $shortcutFolder "AeroSentinel Control Center.lnk"
$targetExe = Join-Path $InstallDir "aerosentinel-desktop.exe"

New-Item -ItemType Directory -Force -Path $shortcutFolder | Out-Null
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($shortcutPath)
$shortcut.TargetPath = $targetExe
$shortcut.WorkingDirectory = $InstallDir
$shortcut.IconLocation = "$targetExe,0"
$shortcut.Description = "AeroSentinel Control Center"
$shortcut.Save()

$regRoot = if ($perMachine) {
    "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall"
} else {
    "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall"
}
$regPath = Join-Path $regRoot "AeroSentinelControlCenter"
$uninstallScript = Join-Path $InstallDir "uninstall-aerosentinel.ps1"

$escapedInstallDir = Escape-SingleQuotedString $InstallDir
$escapedShortcutPath = Escape-SingleQuotedString $shortcutPath
$escapedShortcutFolder = Escape-SingleQuotedString $shortcutFolder
$escapedRegPath = Escape-SingleQuotedString $regPath

$uninstallBody = @"
`$ErrorActionPreference = "SilentlyContinue"
Stop-Process -Name "aerosentinel-desktop", "aerosentinel-control" -ErrorAction SilentlyContinue
Remove-Item -LiteralPath '$escapedShortcutPath' -Force
if (Test-Path '$escapedShortcutFolder') {
    if (-not (Get-ChildItem -LiteralPath '$escapedShortcutFolder' -Force)) {
        Remove-Item -LiteralPath '$escapedShortcutFolder' -Force
    }
}
Remove-Item -LiteralPath '$escapedRegPath' -Recurse -Force
`$cleanup = "Start-Sleep -Seconds 2; Remove-Item -LiteralPath '$escapedInstallDir' -Recurse -Force"
Start-Process -FilePath "powershell.exe" -WindowStyle Hidden -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", `$cleanup)
"@

Set-Content -LiteralPath $uninstallScript -Value $uninstallBody -Encoding UTF8

$estimatedSize = [int]((Get-ChildItem -LiteralPath $InstallDir -Recurse -File | Measure-Object -Property Length -Sum).Sum / 1KB)
New-Item -Path $regPath -Force | Out-Null
New-ItemProperty -Path $regPath -Name "DisplayName" -Value "AeroSentinel Control Center" -PropertyType String -Force | Out-Null
New-ItemProperty -Path $regPath -Name "DisplayVersion" -Value "1.0.0" -PropertyType String -Force | Out-Null
New-ItemProperty -Path $regPath -Name "Publisher" -Value "AeroSentinel" -PropertyType String -Force | Out-Null
New-ItemProperty -Path $regPath -Name "InstallLocation" -Value $InstallDir -PropertyType String -Force | Out-Null
New-ItemProperty -Path $regPath -Name "DisplayIcon" -Value "$targetExe,0" -PropertyType String -Force | Out-Null
New-ItemProperty -Path $regPath -Name "EstimatedSize" -Value $estimatedSize -PropertyType DWord -Force | Out-Null
New-ItemProperty -Path $regPath -Name "UninstallString" -Value "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$uninstallScript`"" -PropertyType String -Force | Out-Null
New-ItemProperty -Path $regPath -Name "QuietUninstallString" -Value "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$uninstallScript`"" -PropertyType String -Force | Out-Null

Write-Host "Installed AeroSentinel Control Center."
Write-Host "Start menu shortcut: $shortcutPath"
Write-Host "Uninstaller registered under: $regPath"

if (!$NoLaunch) {
    Start-Process -FilePath $targetExe -WorkingDirectory $InstallDir
}

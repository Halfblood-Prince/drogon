# Windows Installer

The release workflow produces `aerosentinel-windows-installer.zip`.

Extract the zip, then run PowerShell:

```powershell
Set-ExecutionPolicy -ExecutionPolicy Bypass -Scope Process
.\install-aerosentinel.ps1 -InstallWebView2Runtime
```

You can also double-click `install-aerosentinel.cmd`, which runs the PowerShell installer with WebView2 Runtime installation enabled.

The installer detects the computer architecture and installs the matching `x64` or `arm64` payload. When run as administrator, it installs to `Program Files` and registers a machine-wide uninstaller. Without elevation, it installs for the current user.

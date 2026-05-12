@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-aerosentinel.ps1" -InstallWebView2Runtime %*
endlocal

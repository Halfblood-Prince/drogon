# AeroSentinel Control Center

A Drogon C++ app that serves an AeroSentinel drone mission dashboard and, on Windows, ships a native desktop shell backed by Microsoft Edge WebView2.

## Run Locally

```powershell
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release
.\build\Release\aerosentinel-control.exe
```

Then open `http://localhost:8080/mission/alpha-0426`.

Set `PORT` to run on a different port.
Set `AEROSENTINEL_BIND_ADDRESS=127.0.0.1` when the server should only accept local desktop traffic.

## Native Windows App

The Windows build produces two executables:

- `aerosentinel-control.exe` starts the local Drogon server.
- `aerosentinel-desktop.exe` opens the native desktop window, starts the local server on `127.0.0.1`, and renders the control center through WebView2.

The desktop app requires Microsoft Edge WebView2 Runtime. Windows 11 usually has it already; the installer can install it when run with `-InstallWebView2Runtime`.

## Authentication

The dashboard is protected by a login page. Configure the operator credentials before starting the server:

```powershell
$env:AEROSENTINEL_USER="operator"
$env:AEROSENTINEL_PASSWORD="change-this-password"
.\build\Release\aerosentinel-control.exe
```

For HTTPS deployments, also set `AEROSENTINEL_SECURE_COOKIES=true` so browsers only send the session cookie over TLS.

## CI Binaries

`.github/workflows/build-binaries.yml` builds and uploads release artifacts for Linux, Windows, and macOS on x64 and arm64 GitHub-hosted runners. It also creates `aerosentinel-windows-installer.zip`, which detects the Windows computer architecture and installs the matching x64 or arm64 desktop payload with a Start menu shortcut and registered uninstaller.

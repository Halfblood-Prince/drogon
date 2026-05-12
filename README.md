# AeroSentinel Control Center

A Drogon C++ web app that serves an AeroSentinel drone mission dashboard inspired by the supplied reference image.

## Run Locally

```powershell
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release
.\build\Release\aerosentinel-control.exe
```

Then open `http://localhost:8080/mission/alpha-0426`.

Set `PORT` to run on a different port.

## CI Binaries

`.github/workflows/build-binaries.yml` builds and uploads release artifacts for Linux, Windows, and macOS on x64 and arm64 GitHub-hosted runners.

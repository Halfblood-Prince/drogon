# Linux CLI Installer

The release workflow produces `aerosentinel-linux-installer.tar.gz`.

Extract the archive, then run:

```bash
./install-aerosentinel.sh --user --no-launch
```

The installer detects the machine architecture and installs the matching `x64` or `arm64` payload.

Common options:

```bash
./install-aerosentinel.sh --user
sudo ./install-aerosentinel.sh --system --service
./install-aerosentinel.sh --user --port 9090 --bind 127.0.0.1
```

The installer creates:

- `aerosentinel-control-center` CLI launcher
- `aerosentinel.env` credentials/runtime config
- optional systemd service
- desktop entry
- `uninstall-aerosentinel.sh`

Edit the generated `aerosentinel.env` file before exposing the app beyond localhost.

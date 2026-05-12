# AeroSentinel Flask App

This is a Python/Flask version of the AeroSentinel Control Center. It reuses the shared `../public` dashboard files and implements the same routes as the Drogon app.

## Run

```powershell
cd python
python -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install -r requirements.txt
$env:AEROSENTINEL_USER="admin"
$env:AEROSENTINEL_PASSWORD="admin"
python app.py
```

Open:

```text
http://127.0.0.1:8080/mission/alpha-0426
```

Default development credentials are `admin` / `admin` when `AEROSENTINEL_PASSWORD` is not set.

## Environment

- `PORT`: server port, default `8080`
- `AEROSENTINEL_BIND_ADDRESS`: bind address, default `0.0.0.0`
- `AEROSENTINEL_USER`: login username, default `admin`
- `AEROSENTINEL_PASSWORD`: login password, default `admin`
- `AEROSENTINEL_SECRET_KEY`: Flask session signing key
- `AEROSENTINEL_SECURE_COOKIES`: set to `true` behind HTTPS

## Smoke Test

Start the Flask app in one terminal, then run:

```powershell
python smoke_test.py --base-url http://127.0.0.1:8080
```

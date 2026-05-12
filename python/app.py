from __future__ import annotations

import hmac
import os
import secrets
from pathlib import Path

from flask import Flask, jsonify, make_response, redirect, request, send_from_directory, session


ROOT_DIR = Path(__file__).resolve().parent.parent
PUBLIC_DIR = ROOT_DIR / "public"


def env_or_default(name: str, fallback: str) -> str:
    value = os.environ.get(name)
    return value if value else fallback


def env_flag(name: str) -> bool:
    return env_or_default(name, "").lower() in {"1", "true", "yes", "on"}


def no_store(response):
    response.headers["Cache-Control"] = "no-store"
    return response


def create_app() -> Flask:
    app = Flask(__name__, static_folder=None)
    app.secret_key = env_or_default("AEROSENTINEL_SECRET_KEY", secrets.token_hex(32))
    app.config.update(
        SESSION_COOKIE_HTTPONLY=True,
        SESSION_COOKIE_SAMESITE="Strict",
        SESSION_COOKIE_SECURE=env_flag("AEROSENTINEL_SECURE_COOKIES"),
    )

    username = env_or_default("AEROSENTINEL_USER", "admin")
    password = env_or_default("AEROSENTINEL_PASSWORD", "admin")

    if "AEROSENTINEL_PASSWORD" not in os.environ:
        app.logger.warning(
            "AEROSENTINEL_PASSWORD is not set; using development credentials "
            "username='%s', password='admin'.",
            username,
        )
    if "AEROSENTINEL_SECRET_KEY" not in os.environ:
        app.logger.warning(
            "AEROSENTINEL_SECRET_KEY is not set; generated sessions will reset on restart."
        )

    def is_authenticated() -> bool:
        return session.get("authenticated") is True

    def require_auth_page():
        if not is_authenticated():
            return redirect("/login")
        return None

    def login_page(show_error: bool = False):
        error = (
            '<p class="auth-error">Invalid username or password.</p>'
            if show_error
            else ""
        )
        html = f"""<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>AeroSentinel Login</title>
    <link rel="stylesheet" href="/styles.css">
  </head>
  <body class="auth-body">
    <main class="auth-shell">
      <section class="auth-panel">
        <div class="auth-brand">
          <div class="brand-mark" aria-hidden="true"><span></span></div>
          <div><strong>AeroSentinel</strong><small>CONTROL CENTER</small></div>
        </div>
        <p class="auth-kicker">Secure mission access</p>
        <h1>Operator Sign In</h1>
        <form class="auth-form" method="post" action="/login">
          <label>Username<input type="text" name="username" autocomplete="username" required autofocus></label>
          <label>Password<input type="password" name="password" autocomplete="current-password" required></label>
          {error}
          <button type="submit">Unlock Dashboard</button>
        </form>
      </section>
    </main>
  </body>
</html>"""
        return no_store(make_response(html))

    @app.get("/")
    def root():
        auth_redirect = require_auth_page()
        if auth_redirect:
            return auth_redirect
        return send_from_directory(PUBLIC_DIR, "index.html")

    @app.get("/mission/alpha-0426")
    def mission_page():
        auth_redirect = require_auth_page()
        if auth_redirect:
            return auth_redirect
        return send_from_directory(PUBLIC_DIR, "index.html")

    @app.get("/index.html")
    def index_page():
        auth_redirect = require_auth_page()
        if auth_redirect:
            return auth_redirect
        return send_from_directory(PUBLIC_DIR, "index.html")

    @app.get("/login")
    def login_get():
        if is_authenticated():
            return redirect("/mission/alpha-0426")
        return login_page(request.args.get("error") == "1")

    @app.post("/login")
    def login_post():
        submitted_username = request.form.get("username", "")
        submitted_password = request.form.get("password", "")

        if hmac.compare_digest(submitted_username, username) and hmac.compare_digest(
            submitted_password, password
        ):
            session.clear()
            session["authenticated"] = True
            session["operator"] = submitted_username
            return no_store(redirect("/mission/alpha-0426", code=303))

        app.logger.warning("Failed login attempt for user '%s'", submitted_username)
        return redirect("/login?error=1", code=303)

    @app.route("/logout", methods=["GET", "POST"])
    def logout():
        session.clear()
        return redirect("/login")

    @app.get("/api/mission/alpha-0426")
    def mission_api():
        if not is_authenticated():
            return jsonify({"error": "authentication_required"}), 401

        return jsonify(
            {
                "id": "ALPHA-0426",
                "status": "ACTIVE",
                "profile": "Search & Inspect",
                "survey": "Ridge Line Survey",
                "drone": "Sentinel-7B",
                "battery": 78,
                "altitude_m": 512,
                "speed_ms": 15.2,
                "distance_km": 1.2,
                "signal_percent": 94,
                "version": "python-flask-1.0.0",
            }
        )

    @app.get("/styles.css")
    def styles():
        return send_from_directory(PUBLIC_DIR, "styles.css")

    @app.get("/app.js")
    def app_js():
        return send_from_directory(PUBLIC_DIR, "app.js")

    @app.get("/assets/<path:filename>")
    def assets(filename: str):
        return send_from_directory(PUBLIC_DIR / "assets", filename)

    return app


app = create_app()


if __name__ == "__main__":
    port = int(env_or_default("PORT", "8080"))
    bind_address = env_or_default("AEROSENTINEL_BIND_ADDRESS", "0.0.0.0")
    app.run(host=bind_address, port=port)

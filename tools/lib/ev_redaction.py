#!/usr/bin/env python3
"""Shared evidence-log redaction helpers.

Private lab repositories may keep board-local secrets, but release evidence must
be redacted by construction.  This module intentionally covers both explicit
key/value forms and ESP8266 SDK WiFi status lines that contain the SSID without
printing a key name.  It also redacts the current literal values found in
allowlisted private-lab secret files, so copied values are removed even when a
log line does not preserve the original macro/key name.
"""
from __future__ import annotations

import argparse
import ast
import functools
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
_REDACTED = "<REDACTED>"
_SECRET_KEY_RE = (
    r"EV_BOARD_NET_WIFI_SSID|EV_BOARD_NET_WIFI_PASSWORD|EV_BOARD_NET_COMMAND_TOKEN|"
    r"WIFI_SSID|WIFI_PASSWORD|COMMAND_TOKEN|SSID|PASSWORD|TOKEN|"
    r"MQTT_BROKER|MQTT_BROKER_URI|MQTT_URI|BROKER_URI|BEARER_TOKEN"
)
_SPACE_KEY_RE = (
    r"EV_BOARD_NET_WIFI_SSID|EV_BOARD_NET_WIFI_PASSWORD|EV_BOARD_NET_COMMAND_TOKEN|"
    r"WIFI_SSID|WIFI_PASSWORD|COMMAND_TOKEN|MQTT_BROKER|MQTT_BROKER_URI|MQTT_URI|BROKER_URI|BEARER_TOKEN"
)
_QUOTED_OR_TOKEN_RE = r"\"(?:\\.|[^\"])*\"|'(?:\\.|[^'])*'|[^,\s\r\n]+"
_DEFINE_RE = re.compile(rf"(?P<prefix>#\s*define\s+(?:{_SECRET_KEY_RE})\s+)(?P<value>{_QUOTED_OR_TOKEN_RE})", re.I)
_ASSIGN_RE = re.compile(rf"(?P<prefix>\b(?:{_SECRET_KEY_RE})\b\s*[=:]\s*)(?P<value>{_QUOTED_OR_TOKEN_RE})", re.I)
_SPACE_ASSIGN_RE = re.compile(rf"(?P<prefix>\b(?:{_SPACE_KEY_RE})\b\s+)(?P<value>{_QUOTED_OR_TOKEN_RE})", re.I)
_WIFI_CONNECTED_RE = re.compile(r"(?P<prefix>wifi:connected\s+with\s+)(?P<value>[^,\r\n]+)", re.I)
_WIFI_SSID_RE = re.compile(rf"(?P<prefix>\bssid\s*[=:]\s*)(?P<value>{_QUOTED_OR_TOKEN_RE})", re.I)
_URI_RE = re.compile(r"(?P<prefix>\b(?:mqtt(?:_broker)?_uri|broker_uri|uri|bearer_token)\s*[=:]\s*)(?P<value>[^\s\r\n]+)", re.I)
_ALLOWED_SECRET_FILES = (
    "bsp/wemos_esp_wroom_02_18650/board_secrets.local.h",
)
_SECRET_DEFINE_RE = re.compile(
    r"^\s*#\s*define\s+"
    r"(?:EV_BOARD_NET_WIFI_SSID|EV_BOARD_NET_WIFI_PASSWORD|EV_BOARD_NET_COMMAND_TOKEN)\s+"
    r"(?P<value>\"(?:\\.|[^\"])*\"|[^\s/]+)"
)
_PLACEHOLDER_FRAGMENTS = (
    "<redacted>",
    "redacted",
    "your_",
    "your-",
    "placeholder",
    "change_me",
    "changeme",
    "example",
    "dummy",
)
_PLACEHOLDER_VALUES = {"", "ssid", "wifi_ssid", "wifi-password", "wifi_password", "password", "token", "none", "null"}


def _redact_match(match: re.Match[str]) -> str:
    return match.group("prefix") + _REDACTED


def _c_string_value(token: str) -> str | None:
    token = token.strip()
    if token.startswith('"') and token.endswith('"'):
        try:
            value = ast.literal_eval(token)
        except (SyntaxError, ValueError):
            return None
        return value if isinstance(value, str) else None
    return token


def _is_placeholder(value: str) -> bool:
    normalized = value.strip().strip('"').strip().lower()
    if normalized in _PLACEHOLDER_VALUES:
        return True
    return any(fragment in normalized for fragment in _PLACEHOLDER_FRAGMENTS)


@functools.lru_cache(maxsize=4)
def _allowed_secret_values(root_text: str) -> tuple[str, ...]:
    root = Path(root_text)
    values: set[str] = set()
    for rel in _ALLOWED_SECRET_FILES:
        path = root / rel
        try:
            lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()
        except OSError:
            continue
        for line in lines:
            match = _SECRET_DEFINE_RE.match(line)
            if match is None:
                continue
            value = _c_string_value(match.group("value"))
            if value and not _is_placeholder(value):
                values.add(value)
    return tuple(sorted(values, key=len, reverse=True))


def redact_text(text: str, *, root: Path | None = None) -> str:
    """Return text with known private evidence values replaced by a sentinel.

    The function never prints or returns the literal secret values.  Private
    values are loaded only from allowlisted board-local secret files and are used
    solely for in-memory replacement in evidence/log text.
    """
    out = text
    for pattern in (_DEFINE_RE, _ASSIGN_RE, _SPACE_ASSIGN_RE, _WIFI_CONNECTED_RE, _WIFI_SSID_RE, _URI_RE):
        out = pattern.sub(_redact_match, out)
    root_path = ROOT if root is None else root
    for secret in _allowed_secret_values(str(root_path.resolve())):
        if secret:
            out = out.replace(secret, _REDACTED)
    return out


def self_test() -> None:
    cases = [
        ('#define EV_BOARD_NET_WIFI_SSID "real-ssid"', '#define EV_BOARD_NET_WIFI_SSID <REDACTED>'),
        ('EV_BOARD_NET_WIFI_PASSWORD=real-password', 'EV_BOARD_NET_WIFI_PASSWORD=<REDACTED>'),
        ('WIFI_SSID: real-ssid', 'WIFI_SSID: <REDACTED>'),
        ('ssid: real-ssid', 'ssid: <REDACTED>'),
        ('I (7223) wifi:connected with real-ssid, aid = 23', 'I (7223) wifi:connected with <REDACTED>, aid = 23'),
        ('COMMAND_TOKEN real-token', 'COMMAND_TOKEN <REDACTED>'),
        ('EV_BOARD_NET_COMMAND_TOKEN real-token', 'EV_BOARD_NET_COMMAND_TOKEN <REDACTED>'),
        ('mqtt_broker_uri=mqtt://user:pass@broker.local', 'mqtt_broker_uri=<REDACTED>'),
        ('Bearer_Token: abc.def.ghi', 'Bearer_Token: <REDACTED>'),
    ]
    for raw, expected in cases:
        observed = redact_text(raw)
        assert observed == expected, (raw, observed, expected)
    assert "real-ssid" not in redact_text("wifi:connected with real-ssid, aid = 1")
    tmp_root = ROOT / "build" / "redaction-value-self-test"
    secret_dir = tmp_root / "bsp" / "wemos_esp_wroom_02_18650"
    secret_dir.mkdir(parents=True, exist_ok=True)
    (secret_dir / "board_secrets.local.h").write_text(
        '#define EV_BOARD_NET_WIFI_SSID "literal-ssid-from-allowlist"\n'
        '#define EV_BOARD_NET_WIFI_PASSWORD "literal-password-from-allowlist"\n'
        '#define EV_BOARD_NET_COMMAND_TOKEN "literal-token-from-allowlist"\n',
        encoding="utf-8",
    )
    observed = redact_text("copied value literal-password-from-allowlist without key", root=tmp_root)
    assert "literal-password-from-allowlist" not in observed
    assert observed == "copied value <REDACTED> without key"
    print("EV_REDACTION_SELF_TEST PASS")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    parser.error("no action requested")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())

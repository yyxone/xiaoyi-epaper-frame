"""Apply a verified Xiaoyi Frame application image over the provisioned private LAN."""

import panel
import argparse
import ctypes
from ctypes import wintypes
from hashlib import sha256
import hmac
from http.client import HTTPConnection, HTTPException
from ipaddress import IPv4Address, IPv4Network
import json
import os
from pathlib import Path
import re
import time


BOARD = "XIAOYI-FRAME-S3-N16R8"
PORT = 28463
SLOT_BYTES = 0x300000
TRIAL_WINDOW_SECONDS = 75  # ConfirmMs on Xiaoyi Frame, including time to rejoin Wi-Fi.
PRIVATE_LAN = tuple(IPv4Network(prefix) for prefix in ("10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16"))



class Blob(ctypes.Structure):
    _fields_ = [("size", wintypes.DWORD), ("data", ctypes.POINTER(ctypes.c_ubyte))]


def dpapi(data, protect):
    storage = (ctypes.c_ubyte * len(data)).from_buffer_copy(data)
    source = Blob(len(data), storage)
    output = Blob()
    crypt = ctypes.WinDLL("crypt32", use_last_error=True)
    function = crypt.CryptProtectData if protect else crypt.CryptUnprotectData
    function.argtypes = (
        ctypes.POINTER(Blob), ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p,
        ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(Blob),
    )
    function.restype = wintypes.BOOL
    if not function(ctypes.byref(source), None, None, None, None, 1, ctypes.byref(output)):
        raise ctypes.WinError(ctypes.get_last_error())
    kernel = ctypes.WinDLL("kernel32")
    kernel.LocalFree.argtypes = (ctypes.c_void_p,)
    kernel.LocalFree.restype = ctypes.c_void_p
    try:
        return ctypes.string_at(output.data, output.size)
    finally:
        kernel.LocalFree(output.data)


def pairing_key(path, create=False):
    if create and not path.exists():
        raw = os.urandom(32)
        stored = dpapi(raw, True) if os.name == "nt" else raw
        try:
            fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        except FileExistsError:
            pass
        else:
            with os.fdopen(fd, "wb") as out:
                out.write(stored)
    if os.name != "nt" and path.stat().st_mode & 0o077:
        raise ValueError("OTA key must have mode 0600")
    stored = path.read_bytes()
    key = dpapi(stored, False) if os.name == "nt" else stored
    if len(key) != 32:
        raise ValueError("Invalid paired OTA key")
    return key


def proof(key, *fields):
    return hmac.new(key, "|".join(fields).encode("ascii"), "sha256").hexdigest()


def exchange(host, method, route, body=None, headers=None, timeout=5):
    addr = IPv4Address(host)
    if not any(addr in network for network in PRIVATE_LAN):
        raise ValueError("OTA requires a private LAN address")
    conn = HTTPConnection(str(addr), PORT, timeout=timeout)
    try:
        conn.request(method, route, body=body, headers=headers or {})
        response = conn.getresponse()
        payload = response.read(8192)
        return response.status, json.loads(payload)
    finally:
        conn.close()


def status(host, key, mac):
    code, state = exchange(host, "GET", "/ota/status")
    if code != 200:
        raise RuntimeError(f"Device OTA status rejected the request ({code})")
    if (state["mac"] != mac or state["board"] != BOARD or state["slot"] not in {"app0", "app1"}
            or not re.fullmatch(r"[0-9a-f]{32}", state["nonce"])):
        raise RuntimeError("OTA device identity mismatch")
    expected = proof(
        key, "diy-ota-status-v1", state["mac"], state["board"], state["version"],
        state["slot"], str(int(state["pending"])), str(int(state["ready"])), state["nonce"],
    )
    if not hmac.compare_digest(state["proof"], expected):
        raise RuntimeError("Device OTA status authentication failed")
    return state


def signed(state, key, action, *fields):
    return {
        "X-OTA-Nonce": state["nonce"],
        "X-OTA-Proof": proof(
            key, "diy-ota-request-v1", state["mac"], state["version"], state["nonce"], action,
            "|".join(fields),
        ),
    }


def await_status(host, key, mac, check, timeout=TRIAL_WINDOW_SECONDS):
    deadline = time.monotonic() + timeout
    last_seen = "no authenticated response"
    while time.monotonic() < deadline:
        try:
            state = status(host, key, mac)
        except (OSError, HTTPException, json.JSONDecodeError):
            time.sleep(1)
            continue
        if check(state):
            return state
        last_seen = f"{state['version']} in {state['slot']}, pending={state['pending']}"
        time.sleep(1)
    raise RuntimeError(f"Expected firmware state not observed in {timeout}s. Last seen: {last_seen}")


def summary(state):
    return {k: state[k] for k in ("board", "version", "slot", "pending", "ready")}


def update(host, key, mac, image, version, trial_rollback=False, *, before_confirm=None):
    app = image.read_bytes()
    if (not 0 < len(app) <= SLOT_BYTES or app[0] != 0xe9 or
            BOARD.encode("ascii") + b"\0" not in app or version.encode("ascii") + b"\0" not in app):
        raise ValueError("Expected a compiled app image for this board and requested version")
    start = status(host, key, mac)
    if not start["ready"] or start["pending"] or start["version"] == version:
        raise RuntimeError("Expected a healthy, confirmed device running a different version")
    digest = sha256(app).hexdigest()
    headers = signed(start, key, "upload", digest, str(len(app)))
    headers.update({"Content-Type": "application/octet-stream", "X-App-SHA256": digest})
    print("Sending firmware", version, f"bytes={len(app)} sha256={digest}", flush=True)
    try:
        code, result = exchange(host, "PUT", "/ota/app", app, headers, timeout=50)
        if code != 202 or result.get("state") != "restarting":
            raise RuntimeError(f"Update rejected ({code}, {result.get('state', 'unknown')})")
    except (OSError, HTTPException):
        # A reply can be lost as the ESP32 restarts. Require authenticated readback.
        pass
    next_boot = await_status(host, key, mac, lambda state: state["version"] == version)
    if next_boot["slot"] == start["slot"] or not next_boot["pending"]:
        raise RuntimeError("Updated image did not boot in the expected pending OTA slot")
    print("First boot", json.dumps(summary(next_boot)), flush=True)
    if not next_boot["ready"]:
        raise RuntimeError("Updated image did not pass hardware checks and will roll back")
    if trial_rollback:
        restored = await_status(
            host, key, mac, lambda state: state["version"] == start["version"]
            and state["slot"] == start["slot"] and not state["pending"] and state["ready"], timeout=95,
        )
        print("Rollback verified", json.dumps(summary(restored)), flush=True)
        return
    if before_confirm is not None:
        before_confirm(host, next_boot)
        # The validation may have taken time. Bind confirmation to a fresh challenge.
        next_boot = status(host, key, mac)
        if next_boot["version"] != version or next_boot["slot"] == start["slot"] or not next_boot["pending"] or not next_boot["ready"]:
            raise RuntimeError("Validated trial is no longer the active pending image")
    code, result = exchange(host, "POST", "/ota/confirm", b"", signed(next_boot, key, "confirm"))
    if code != 200 or result.get("state") != "confirmed":
        raise RuntimeError(f"Update confirmation failed ({code})")
    verified = status(host, key, mac)
    if verified["version"] != version or verified["slot"] != next_boot["slot"] or verified["pending"] or not verified["ready"]:
        raise RuntimeError("Device did not read back the confirmed firmware")
    print("Update verified", json.dumps(summary(verified)), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, default=panel.DEFAULT_CONFIG)
    sub = parser.add_subparsers(dest="action", required=True)
    usb = sub.add_parser("provision")
    usb.add_argument("--port", required=True)
    sub.add_parser("status")
    install = sub.add_parser("update")
    install.add_argument("image", type=Path)
    install.add_argument("version")
    args = parser.parse_args()
    cfg = panel.load_config(args.config)
    mac = cfg["mac"]
    key_path = args.config.parent / ("ota-" + mac.replace(":", "") + (".dpapi" if os.name == "nt" else ".key"))
    if args.action == "provision":
        with panel.open_serial(args.port) as port:
            info = panel.serial_info(port)
            if info["mac"] != mac:
                raise ValueError("Unexpected board or missing USB OTA support")
            key = pairing_key(key_path, create=True)
            if len(key) != 32 or not panel.exchange(port, b"EPD OTAKEY " + key.hex().encode()).get("ota_key_saved"):
                raise RuntimeError("Board did not acknowledge OTA provisioning")
    key = pairing_key(key_path)
    host = panel.find_host(cfg)
    if args.action == "update":
        if not panel.FIRMWARE_FAMILY.fullmatch(args.version):
            raise ValueError("Expected a xiaoyi-frame release")
        live = panel.request(host, cfg)
        if live.get("slides") or live.get("state") in ("accepted", "refreshing"):
            raise RuntimeError("The frame is busy or has a saved slideshow. Keep it playing, or clear it in the household page before updating.")
        update(host, key, mac, args.image, args.version)
    else:
        print(json.dumps(summary(status(host, key, mac))))

if __name__ == "__main__":
    main()

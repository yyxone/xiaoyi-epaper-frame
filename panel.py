"""Xiaoyi Frame image preparation, USB provisioning and LAN upload. No firmware flashing."""
import argparse
import getpass
import hashlib
import epd_image
import ipaddress
import json
import os
from pathlib import Path
import re
import secrets
import socket
import time
from http.client import HTTPConnection, HTTPException

FIRMWARE = "xiaoyi-frame-1"
FIRMWARE_FAMILY = re.compile(r"xiaoyi-frame-[1-9][0-9]*")
FRAME_BYTES = 192000
DEFAULT_CONFIG = Path(os.environ.get("LOCALAPPDATA") or os.environ.get("XDG_CONFIG_HOME") or Path.home() / ".config") / "xiaoyi-frame" / "panel.json"

def prepare(image, mode="pad", preview=None, *, center_x=0.5, center_y=None, photo=False):
    from PIL import Image, ImageEnhance, ImageOps, ImageStat

    with Image.open(image) as src:
        rgb = ImageOps.exif_transpose(src).convert("RGB")
    if center_y is None:
        portrait = rgb.height > rgb.width
        center_y = 0.20 if photo and portrait else 0.5
    canvas = epd_image.fit(rgb, mode, center_y, center_x)
    if photo:
        # Estimate exposure from the portion that will be shown, excluding padding.
        reference = canvas if mode == "crop" else rgb
        sample = ImageOps.contain(reference, (160, 160)).convert("L")
        median = ImageStat.Stat(sample).median[0]
        lift = 0.15 * max(0, 112 - median) / 112
        if lift:
            canvas = ImageEnhance.Brightness(canvas).enhance(1 + lift)
    indices = epd_image.quantize(canvas)
    frame = epd_image.pack_indices(indices)
    if preview:
        out = Image.new("RGB", (800, 480))
        out.putdata([epd_image.PALETTE[i][1] for i in indices])
        Path(preview).parent.mkdir(parents=True, exist_ok=True)
        out.save(preview)
    return frame

def validate_frame(frame):
    if len(frame) != FRAME_BYTES or any((b >> 4) > 5 or (b & 15) > 5 for b in frame):
        raise ValueError("Expected exactly 192000 bytes of packed six-colour pixels")

def request(host, config, method="GET", route="/status", frame=None):
    conn = HTTPConnection(host, int(config.get("port", 28463)), timeout=5)
    headers = {"Authorization": "Bearer " + config["token"]}
    if frame is not None:
        headers.update({"Content-Type": "application/octet-stream", "X-Frame-SHA256": hashlib.sha256(frame).hexdigest()})
    try:
        conn.request(method, route, body=frame, headers=headers)
        res = conn.getresponse()
        data = res.read(4097)
        if len(data) > 4096:
            raise ValueError("Oversized board response")
        value = json.loads(data)
        if not isinstance(value, dict):
            raise ValueError("Invalid board response")
        if res.status not in (200, 202):
            raise RuntimeError(f"Board rejected request: HTTP {res.status}, {value.get('failure', 'unknown')}")
        verify_identity(value, config)
        return value
    finally:
        conn.close()

def verify_identity(reply, config):
    if not FIRMWARE_FAMILY.fullmatch(str(reply.get("firmware", ""))) or reply.get("mac") != config["mac"]:
        raise ValueError("Device identity mismatch; stopped without retrying")

def load_config(path):
    value = json.loads(Path(path).read_text(encoding="utf-8-sig"))
    if not re.fullmatch(r"[a-f0-9]{48}", value.get("token", "")):
        raise ValueError("Invalid device token")
    if not re.fullmatch(r"(?:[a-f0-9]{2}:){5}[a-f0-9]{2}", value.get("mac", "")):
        raise ValueError("Invalid device MAC")
    return value

def find_host(config):
    candidates = []
    hostname = config.get("hostname", "")
    if hostname.endswith(".local"):
        try:
            candidates.append(socket.gethostbyname(hostname))
        except OSError:
            pass
    candidates.append(config["host"])
    for host in dict.fromkeys(candidates):
        try:
            ip = ipaddress.IPv4Address(host)
            if not ip.is_private or ip.is_loopback or ip.is_multicast or ip.is_unspecified:
                continue
            request(str(ip), config)
            return str(ip)
        except (OSError, HTTPException):
            continue
    raise OSError("Board not reachable at its saved IP or mDNS name; use USB info to check its new IP")

def verify_refresh_duration(receipt):
    duration = receipt.get("refresh_ms")
    if type(duration) is not int or not 10000 <= duration < 120000:
        raise RuntimeError("Missing valid BUSY completion duration")


def push_frame(host, config, frame, timeout=360, poll_interval=1, *, require_ready=False):
    validate_frame(frame)
    digest = hashlib.sha256(frame).hexdigest()
    # Identity check precedes the one and only upload.
    before = request(host, config)
    if require_ready:
        if before.get("connected") is not True or before.get("frame_buffer") is not True:
            raise RuntimeError("Board is offline or its frame buffer is unavailable")
        if before.get("state") not in ("idle", "displayed", "failed"):
            raise RuntimeError(f"Board is not idle: {before.get('state')}; no frame was sent")
        if before.get("state") == "displayed" and before.get("sha256") == digest:
            verify_refresh_duration(before)
            return {**before, "already_displayed": True}
    end = time.monotonic() + timeout
    try:
        receipt = request(host, config, "PUT", "/frame", frame)
    except (OSError, HTTPException, ValueError) as error:
        raise RuntimeError(f"Frame submission is unconfirmed: {error}. Do not resend") from error
    if receipt.get("sha256") != digest or receipt.get("state") not in ("accepted", "refreshing", "displayed"):
        raise RuntimeError("Board did not acknowledge this exact frame; do not retry automatically")
    while True:
        if receipt.get("sha256") != digest:
            raise RuntimeError("Board reports a different frame; stopped")
        if receipt.get("state") == "displayed":
            verify_refresh_duration(receipt)
            return receipt
        if receipt.get("state") not in ("accepted", "refreshing"):
            raise RuntimeError("Display failed: " + str(receipt.get("failure", receipt.get("state"))))
        if time.monotonic() >= end:
            raise TimeoutError("Upload acknowledged; physical refresh is unconfirmed. Do not automatically resend")
        time.sleep(poll_interval)
        try:
            receipt = request(host, config)
        except (OSError, HTTPException):
            # The firmware's single display task blocks HTTP during the physical refresh.
            continue
        except (ValueError, RuntimeError) as error:
            raise RuntimeError(f"Board acknowledged this frame; refresh is unconfirmed: {error}. Do not resend") from error

def fraction(raw):
    value = float(raw)
    if not 0 <= value <= 1:
        raise argparse.ArgumentTypeError("expected a value from 0 to 1")
    return value


def send_prepared(args):
    with args.frame.open("rb") as handle:
        frame = handle.read(FRAME_BYTES + 1)
    validate_frame(frame)
    if hashlib.sha256(frame).hexdigest() != args.sha256.lower():
        raise ValueError("Prepared frame does not match the reviewed SHA256; no frame was sent")
    args.receipt.parent.mkdir(parents=True, exist_ok=True)
    if args.receipt.exists():
        raise FileExistsError(f"Receipt path already exists: {args.receipt}")

    config = load_config(args.config)
    result = push_frame(find_host(config), config, frame, require_ready=True)
    try:
        with args.receipt.open("x", encoding="utf-8") as handle:
            json.dump(result, handle, ensure_ascii=False, indent=2)
            handle.write("\n")
    except OSError as error:
        raise RuntimeError(f"Board confirmed displayed for {result['sha256']}, "
                           f"but the receipt could not be saved: {error}. Do not resend") from error
    return result

def open_serial(port):
    import serial
    connection = serial.Serial(port=None, baudrate=115200, timeout=.3, write_timeout=5)
    connection.dtr = False
    connection.rts = False
    connection.port = port
    connection.open()
    return connection

def exchange(connection, command, timeout=5):
    connection.reset_input_buffer()
    connection.write(command + b"\n")
    connection.flush()
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        line = connection.readline(1024)
        if line.startswith(b"{"):
            try:
                value = json.loads(line)
                if isinstance(value, dict):
                    return value
            except (ValueError, UnicodeError):
                continue
    raise TimeoutError("No reply from the simple e-paper firmware")

def serial_info(connection):
    reply = exchange(connection, b"EPD INFO")
    if not FIRMWARE_FAMILY.fullmatch(str(reply.get("firmware", ""))) or not re.fullmatch(r"(?:[a-f0-9]{2}:){5}[a-f0-9]{2}", reply.get("mac", "")):
        raise ValueError("This port is not running the standalone e-paper firmware")
    return reply

def save_pairing(path, config):
    import tempfile
    with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=path.parent, delete=False) as output:
        tmp = Path(output.name)
        try:
            json.dump(config, output, indent=2)
            output.flush()
            os.fsync(output.fileno())
        except BaseException:
            output.close()
            tmp.unlink(missing_ok=True)
            raise
    try:
        os.replace(tmp, path)
    finally:
        tmp.unlink(missing_ok=True)


def configure(args):
    args.config.parent.mkdir(parents=True, exist_ok=True)
    with open_serial(args.port) as connection:
        before = serial_info(connection)
        if before["mac"] != args.mac.lower():
            raise ValueError("USB board MAC differs from the one selected with --mac")
        if before.get("hardware_ready") is not True:
            raise ValueError("Expected ESP32-S3 with 16 MB flash and 8 MB OPI PSRAM")
        if not before.get("frame_buffer"):
            raise RuntimeError("Board PSRAM/frame buffer is unavailable")
        if args.config.exists() and load_config(args.config)["mac"] != before["mac"]:
            raise ValueError("Existing configuration belongs to another board; choose a new --config path")
        password = getpass.getpass("2.4 GHz Wi-Fi password (hidden): ")
        ssid, password = args.ssid.encode("utf-8"), password.encode("utf-8")
        if not 1 <= len(ssid) <= 32 or not 8 <= len(password) <= 63 or b"\0" in ssid + password:
            raise ValueError("SSID/password length invalid")
        token = secrets.token_hex(24)
        payload = bytes([len(ssid), len(password)]) + ssid + password + token.encode("ascii")
        reply = exchange(connection, b"EPD CONFIG " + payload.hex().encode("ascii"))
        del password, payload
        if reply.get("configured") is not True:
            raise RuntimeError("Board did not save the network configuration")
        end = time.monotonic() + 45
        while time.monotonic() < end:
            time.sleep(1)
            info = serial_info(connection)
            if info["mac"] != before["mac"]:
                raise RuntimeError("USB device identity changed")
            if info.get("connected"):
                config = {"host": info["ip"], "hostname": info["host"], "port": 28463, "mac": info["mac"], "token": token}
                request(config["host"], config)
                save_pairing(args.config, config)
                return {"configured": True, "mac": info["mac"], "ip": info["ip"], "config_path": str(args.config.resolve())}
        raise TimeoutError("Configuration saved on the board, but Wi-Fi connection is unconfirmed; run configure again after checking the network")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    for name in ("prepare", "push"):
        p = sub.add_parser(name)
        p.add_argument("image", type=Path)
        p.add_argument("--mode", choices=("pad", "crop"), default="pad")
        p.add_argument("--preview", type=Path)
        if name == "prepare": p.add_argument("--out", required=True, type=Path)
        else: p.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    p = sub.add_parser("prepare-photo", help="Build a full-screen six-colour photo and preview offline")
    p.add_argument("image", type=Path)
    p.add_argument("--output-dir", required=True, type=Path, help="New directory for frame.bin and preview.png")
    p.add_argument("--mode", choices=("pad", "crop"), default="crop")
    p.add_argument("--center-x", type=fraction, default=0.5, help="Crop position from left (0) to right (1)")
    p.add_argument("--center-y", type=fraction, help="Crop position from top (0) to bottom (1)")
    p = sub.add_parser("push-frame", help="Send a previously previewed, hash-verified frame")
    p.add_argument("frame", type=Path)
    p.add_argument("--sha256", required=True, help="Frame SHA256 returned by prepare-photo")
    p.add_argument("--receipt", required=True, type=Path)
    p.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    p = sub.add_parser("status"); p.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    p = sub.add_parser("info"); p.add_argument("--port", required=True)
    p = sub.add_parser("configure")
    p.add_argument("--port", required=True); p.add_argument("--ssid", required=True)
    p.add_argument("--mac", required=True, help="MAC printed by info for this USB port")
    p.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    args = parser.parse_args()
    try:
        if args.action in ("prepare", "push"):
            frame = prepare(args.image, args.mode, args.preview); validate_frame(frame)
            if args.action == "prepare":
                args.out.parent.mkdir(parents=True, exist_ok=True); args.out.write_bytes(frame)
                result = {"bytes": len(frame), "sha256": hashlib.sha256(frame).hexdigest(), "path": str(args.out.resolve())}
            else:
                config = load_config(args.config)
                result = push_frame(find_host(config), config, frame)
        elif args.action == "prepare-photo":
            args.output_dir.mkdir(parents=True)
            preview = args.output_dir / "preview.png"
            path = args.output_dir / "frame.bin"
            frame = prepare(args.image, args.mode, preview, center_x=args.center_x, center_y=args.center_y, photo=True)
            validate_frame(frame)
            path.write_bytes(frame)
            result = {"bytes": len(frame), "sha256": hashlib.sha256(frame).hexdigest(),
                      "path": str(path.resolve()), "preview": str(preview.resolve())}
        elif args.action == "push-frame":
            result = send_prepared(args)
        elif args.action == "status":
            config = load_config(args.config); result = request(find_host(config), config)
        elif args.action == "info":
            with open_serial(args.port) as connection: result = serial_info(connection)
        else:
            result = configure(args)
        print(json.dumps(result, ensure_ascii=False))
    except (ValueError, KeyError, OSError, RuntimeError, HTTPException) as error:
        parser.exit(1, str(error) + "\n")

if __name__ == "__main__":
    main()

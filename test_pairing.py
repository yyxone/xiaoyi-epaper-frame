import json
import os
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import ota
import panel


class PairingTests(unittest.TestCase):
    def test_usb_mismatch_rejected_before_password_or_write(self):
        with TemporaryDirectory() as tmp:
            args = SimpleNamespace(port="test-port", config=Path(tmp)/"panel.json",
                                   mac="02:00:00:00:00:01", ssid="example")
            other = dict(mac="02:00:00:00:00:02", firmware=panel.FIRMWARE, frame_buffer=True)
            with (patch.object(panel, "open_serial"), patch.object(panel, "serial_info", return_value=other),
                  patch.object(panel.getpass, "getpass") as password, patch.object(panel, "exchange") as send):
                with self.assertRaisesRegex(ValueError, "MAC"):
                    panel.configure(args)
                password.assert_not_called()
                send.assert_not_called()
            self.assertFalse(args.config.exists())

    def test_unsupported_memory_rejected_before_password(self):
        address = "02:00:00:00:00:01"
        with TemporaryDirectory() as tmp:
            args = SimpleNamespace(port="test-port", config=Path(tmp)/"panel.json", mac=address)
            info = dict(mac=address, firmware=panel.FIRMWARE, hardware_ready=False, frame_buffer=True)
            with (patch.object(panel, "open_serial"), patch.object(panel, "serial_info", return_value=info),
                  patch.object(panel.getpass, "getpass") as password):
                with self.assertRaisesRegex(ValueError, "16 MB flash"):
                    panel.configure(args)
                password.assert_not_called()

    def test_usb_pairs_selected_board_and_saves_no_wifi_password(self):
        with TemporaryDirectory() as tmp:
            path = Path(tmp)/"panel.json"
            address = "02:00:00:00:00:01"
            args = SimpleNamespace(port="test-port",config=path,mac=address,ssid="example")
            info = dict(mac=address, firmware=panel.FIRMWARE, frame_buffer=True, hardware_ready=True,
                        connected=True, ip="192.0.2.1", host="example.local")
            with (patch.object(panel, "open_serial"), patch.object(panel, "serial_info", return_value=info),
                  patch.object(panel.getpass, "getpass", return_value="test-passphrase"),
                  patch.object(panel.time, "sleep"), patch.object(panel, "request") as verify,
                  patch.object(panel, "exchange", return_value={"configured": True})):
                result = panel.configure(args)
                self.assertTrue(result["configured"])
                verify.assert_called_once()
            config = json.loads(path.read_text())
            self.assertEqual(config["mac"], address)
            self.assertSetEqual(set(config), {"host","hostname","port","mac","token"})
            if os.name != "nt":
                self.assertEqual(path.stat().st_mode & 0o077, 0)

    def test_active_slideshow_blocks_ota_without_upload(self):
        cfg = {"mac": "02:00:00:00:00:01"}
        with (patch("sys.argv", ["ota.py","update","unused.bin","xiaoyi-frame-2"]),
              patch.object(panel, "load_config", return_value=cfg),
              patch.object(ota, "pairing_key", return_value=b"x" * 32),
              patch.object(panel, "find_host", return_value="192.0.2.1"),
              patch.object(panel, "request", return_value={"slides": 3, "state": "displayed"}),
              patch.object(ota, "update") as send):
            with self.assertRaisesRegex(RuntimeError, "saved slideshow"):
                ota.main()
            send.assert_not_called()

    def test_ota_key_is_stable_per_pairing_and_unique_between_devices(self):
        with TemporaryDirectory() as tmp, patch.object(ota, "dpapi", side_effect=lambda data, protect: data):
            first = Path(tmp)/"first.key"
            second = Path(tmp)/"second.key"
            a = ota.pairing_key(first, create=True)
            self.assertEqual(len(a), 32)
            self.assertEqual(ota.pairing_key(first, create=True), a)
            self.assertEqual(ota.pairing_key(first), a)
            self.assertNotEqual(ota.pairing_key(second, create=True), a)

if __name__ == "__main__":
    unittest.main()

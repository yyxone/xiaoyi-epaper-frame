from contextlib import redirect_stdout
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from io import StringIO
from itertools import count
import json
from pathlib import Path
from types import SimpleNamespace
import tempfile
import threading
import unittest
from unittest.mock import patch
from PIL import Image
import panel
import ota

MAC = '00:11:22:33:44:55'
CONFIG = {'mac': MAC, 'token': '0'*48}
FRAME = bytes([0x01, 0x23, 0x45]) * 64000
DIGEST = hashlib.sha256(FRAME).hexdigest()
def reply(state='displayed', **kw):
    return dict(firmware=panel.FIRMWARE, mac=MAC, state=state, sha256=DIGEST, refresh_ms=28100, **kw)

class Tests(unittest.TestCase):
    def test_ota_trial_can_rejoin_wifi_after_30_seconds(self):
        trial = dict(version="xiaoyi-frame-2", slot="app0", pending=True, ready=True)
        with (patch.object(ota, "status", side_effect=[TimeoutError()] * 42 + [trial]),
              patch.object(ota.time, "monotonic", side_effect=count()),
              patch.object(ota.time, "sleep")):
            observed = ota.await_status("192.168.1.1", b"", MAC, lambda state: state["pending"])
        self.assertEqual(observed, trial)

    def test_conversion_size_and_padding(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp)/'tall.png'; preview = Path(tmp)/'preview.png'
            Image.new('RGB',(120,300),'red').save(source)
            frame = panel.prepare(source, preview=preview)
            panel.validate_frame(frame)
            with Image.open(preview) as image:
                self.assertEqual(image.size,(800,480))
            self.assertEqual(len(frame),192000)

    def test_exif_orientation(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp)/'oriented.jpg'
            image = Image.new('RGB',(480,800),'white')
            exif = Image.Exif(); exif[274] = 6
            image.save(source,exif=exif)
            panel.validate_frame(panel.prepare(source))

    def test_prepare_photo_defaults_to_fullscreen_portrait_crop(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / 'portrait.png'
            yellow, white = (216, 177, 48), (200, 197, 182)
            image = Image.new('RGB', (120, 300), white)
            image.paste(yellow, (0, 0, 120, 150))
            image.save(source)
            for name, options, color in (
                ('default', [], yellow),
                ('bottom', ['--center-y', '1'], white),
                ('pad', ['--mode', 'pad'], white),
            ):
                with self.subTest(name=name):
                    output = root / name
                    argv = ['panel.py', 'prepare-photo', str(source), '--output-dir', str(output), *options]
                    log = StringIO()
                    with patch('sys.argv', argv), redirect_stdout(log), patch.object(panel, 'load_config') as config:
                        panel.main()
                    config.assert_not_called()
                    self.assertEqual(json.loads(log.getvalue())['sha256'],
                                     hashlib.sha256((output / 'frame.bin').read_bytes()).hexdigest())
                    with Image.open(output / 'preview.png') as preview:
                        self.assertEqual(preview.size, (800, 480))
                        self.assertEqual(preview.getpixel((0, 0)), color)
                    with (patch('sys.argv', argv), patch('sys.stderr', new=StringIO()),
                          patch.object(panel, 'prepare') as prepare, self.assertRaises(SystemExit)):
                        panel.main()
                    prepare.assert_not_called()

    def test_push_frame_checks_reviewed_hash_and_saves_receipt(self):
        with tempfile.TemporaryDirectory() as tmp:
            frame = Path(tmp) / 'frame.bin'
            frame.write_bytes(FRAME)
            receipt_path = Path(tmp) / 'receipt.json'
            argv = ['panel.py', 'push-frame', str(frame), '--receipt', str(receipt_path), '--sha256']
            with patch('sys.argv', argv + ['f' * 64]), patch.object(panel, 'load_config') as config:
                with self.assertRaisesRegex(SystemExit, '1'), patch('sys.stderr', new=StringIO()):
                    panel.main()
                config.assert_not_called()

            config = dict(CONFIG)
            before = {**reply('failed', connected=True, frame_buffer=True), 'mac': config['mac']}
            finished = {**reply(), 'mac': config['mac']}
            with (patch('sys.argv', argv + [DIGEST]), redirect_stdout(StringIO()),
                  patch.object(panel, 'load_config', return_value=config),
                  patch.object(panel, 'find_host', return_value='host'),
                  patch.object(panel, 'request', side_effect=[before, finished]) as request):
                panel.main()
            request.assert_called_with('host', config, 'PUT', '/frame', FRAME)
            self.assertEqual(json.loads(receipt_path.read_text(encoding='utf-8')), finished)
            with (patch('sys.argv', argv + [DIGEST]), patch('sys.stderr', new=StringIO()),
                  patch.object(panel, 'load_config') as load_config, self.assertRaises(SystemExit)):
                panel.main()
            load_config.assert_not_called()

    def test_prepared_frame_already_displayed_does_not_upload(self):
        before = reply(connected=True, frame_buffer=True)
        with patch.object(panel, 'request', return_value=before) as request:
            result = panel.push_frame('host', CONFIG, FRAME, require_ready=True)
        self.assertEqual(result, {**before, 'already_displayed': True})
        request.assert_called_once_with('host', CONFIG)

    def test_prepared_frame_does_not_upload_when_board_is_busy_or_unavailable(self):
        unavailable = [reply('accepted', connected=True, frame_buffer=True),
                       reply('idle', connected=False, frame_buffer=True),
                       reply('idle', connected=True, frame_buffer=False)]
        for before in unavailable:
            with patch.object(panel, 'request', return_value=before) as request:
                with self.assertRaises(RuntimeError):
                    panel.push_frame('host', CONFIG, FRAME, require_ready=True)
                request.assert_called_once_with('host', CONFIG)

    def test_invalid_frame_before_any_request(self):
        for data in (b'', FRAME[:-1], bytes([0x67])*192000):
            with self.subTest(length=len(data)), patch.object(panel,'request') as req:
                with self.assertRaises(ValueError): panel.push_frame('host',CONFIG,data)
                req.assert_not_called()

    def test_completion_requires_matching_hash(self):
        wrong = reply(); wrong['sha256']='a'*64
        with patch.object(panel,'request',side_effect=[reply('idle'),reply('accepted'),wrong]):
            with self.assertRaises(RuntimeError): panel.push_frame('host',CONFIG,FRAME,poll_interval=0)

    def test_completion_requires_busy_duration(self):
        for value in (None,0,True,9999,120000):
            bad = reply(); bad['refresh_ms']=value
            with self.subTest(value=value), patch.object(panel,'request',side_effect=[reply('idle'),bad]):
                with self.assertRaises(RuntimeError): panel.push_frame('host',CONFIG,FRAME,poll_interval=0)

    def test_failed_refresh_not_success(self):
        with patch.object(panel,'request',side_effect=[reply('idle'),reply('accepted'),reply('failed',failure='busy_after_reset')]):
            with self.assertRaises(RuntimeError): panel.push_frame('host',CONFIG,FRAME,poll_interval=0)

    def test_lost_poll_does_not_repeat_upload(self):
        with patch.object(panel,'request',side_effect=[reply('idle'),reply('accepted'),OSError('busy'),reply()]) as req:
            result=panel.push_frame('host',CONFIG,FRAME,poll_interval=0)
            self.assertEqual(result['state'],'displayed')
            self.assertEqual(sum(call.args[2:3] == ('PUT',) for call in req.call_args_list),1)

    def test_lost_upload_response_is_reported_as_unconfirmed(self):
        for error in (OSError('disconnected'), ValueError('invalid JSON')):
            with patch.object(panel, 'request', side_effect=[reply('idle'), error]) as request:
                with self.assertRaisesRegex(RuntimeError, 'submission is unconfirmed.*Do not resend'):
                    panel.push_frame('host', CONFIG, FRAME)
                self.assertEqual(request.call_count, 2)

    def test_invalid_poll_response_keeps_upload_acknowledgment(self):
        for error in (RuntimeError('HTTP 503'), ValueError('invalid JSON')):
            responses = [reply('idle'), reply('accepted'), error]
            with patch.object(panel, 'request', side_effect=responses) as request:
                with self.assertRaisesRegex(RuntimeError, 'acknowledged.*refresh is unconfirmed.*Do not resend'):
                    panel.push_frame('host', CONFIG, FRAME, poll_interval=0)
                self.assertEqual(sum(call.args[2:3] == ('PUT',) for call in request.call_args_list), 1)

    def test_timeout_does_not_repeat_upload(self):
        with patch.object(panel,'request',side_effect=[reply('idle'),reply('accepted')]) as req:
            with self.assertRaises(TimeoutError): panel.push_frame('host',CONFIG,FRAME,timeout=0,poll_interval=0)
            self.assertEqual(req.call_count,2)

    def test_wrong_board_rejected(self):
        for field, value in [('mac','99:99:99:99:99:99'),('firmware','other-firmware-26')]:
            wrong=reply(); wrong[field]=value
            with self.assertRaises(ValueError): panel.verify_identity(wrong,CONFIG)

    def test_real_http_upload_and_identity_guard(self):
        received=[]
        class Handler(BaseHTTPRequestHandler):
            mismatch=False
            def log_message(self,*args): pass
            def answer(self,status,body):
                raw=json.dumps(body).encode(); self.send_response(status)
                self.send_header('Content-Length',str(len(raw))); self.end_headers(); self.wfile.write(raw)
            def do_GET(self):
                value=reply('idle')
                if self.mismatch: value['mac']='ff:ff:ff:ff:ff:ff'
                self.answer(200,value)
            def do_PUT(self):
                body=self.rfile.read(int(self.headers['Content-Length']))
                received.append((self.headers['Authorization'],self.headers['X-Frame-SHA256'],body))
                self.answer(200,reply())
        server=ThreadingHTTPServer(('127.0.0.1',0),Handler)
        thread=threading.Thread(target=server.serve_forever,daemon=True); thread.start()
        try:
            cfg=dict(CONFIG,port=server.server_port)
            result=panel.push_frame('127.0.0.1',cfg,FRAME)
            self.assertEqual(result['state'],'displayed')
            self.assertEqual(received,[('Bearer '+'0'*48,DIGEST,FRAME)])
            Handler.mismatch=True
            with self.assertRaises(ValueError): panel.push_frame('127.0.0.1',cfg,FRAME)
            self.assertEqual(len(received),1)
        finally:
            server.shutdown(); server.server_close(); thread.join()

if __name__ == '__main__': unittest.main()

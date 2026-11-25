# Xiaoyi six-color e-paper photo frame

[简体中文](README.md) · **English**

Turn an ESP32-S3 and a six-color e-paper display into a family photo frame. Anyone on your home Wi-Fi can open the frame’s web page on a phone, crop a picture, preview it in six colors and send it to the display. The board serves the page and stores the slideshow in flash. Once it’s set up, you can turn off the computer. No account, cloud service or upload code is required.

## Using the frame at home

The controls on the frame’s web page are in Chinese. The labels below match the ones you’ll see on your phone.

| What you want to do | Where to tap |
| --- | --- |
| See what’s on the screen | Open **当前画面** (Current image), then **放大查看** (Enlarge) to see the last confirmed 800×480 refresh. During a slideshow you can also see the photo count, interval and which photo comes next. |
| Fit a portrait photo to the landscape display | **铺满屏幕** (Fill screen) crops to fill without stretching. Drag to reframe, pinch to zoom or use the adjustment buttons. Choose **保留整张 · 留白** (Fit whole photo) to keep the edges and add margins. Each photo has its own framing. |
| Preview the six colors | Tap **看六色效果** (Preview six colors) in the crop editor. Compare **层次增强** (Enhanced detail) and **经典** (Classic) for each photo. Conversion runs in your browser. |
| Send one photo or start a slideshow | Tap **投当前图** (Show this photo) for a single picture. For a slideshow, select 2 to 12 photos, choose 15, 30 or 60 minutes between changes and tap **开始轮播** (Start slideshow). You can add, select, reorder or remove photos before sending. |
| Edit a slideshow that’s already running | In **管理轮播** (Manage slideshow), select one or two photos and tap **替换所选** (Replace selected). You can also reorder photos, change the interval, use **用所选照片整组换新** (Replace the whole slideshow) or **清空轮播** (Clear slideshow). If another phone changes the list first, the page asks you to review the updated list. |
| Resume after a power cut | The frame saves its place in flash and waits 180 seconds after startup before refreshing. If a refresh fails, your photos stay in the album. Tap **重试轮播** (Retry slideshow) to resume. |

## Hardware and wiring

The build targets an **ESP32-S3 N16R8** with 16 MB flash and 8 MB OPI PSRAM, and a **Waveshare 7.3inch e-Paper HAT (E)** with an 800×480 six-color panel and the GDEP073E01 driver. Check the **(E)** suffix and your board’s pin labels before wiring. The slideshow uses onboard flash, so an SD card is optional.

| HAT (E) | ESP32-S3 |
| --- | --- |
| VCC / GND | 3V3 / GND |
| DIN / SCLK | GPIO4 / GPIO5 |
| CS / DC | GPIO6 / GPIO7 |
| RST / BUSY | GPIO15 / GPIO16 |

[Waveshare panel manual](https://www.waveshare.com/wiki/7.3inch_e-Paper_HAT_%28E%29_Manual) · [GxEPD2 driver](https://github.com/ZinggJM/GxEPD2)

## First installation

Run these commands from the repository root. You’ll need Python 3.11 or later, Arduino CLI and a USB data cable. Replace the sample serial port, Wi-Fi name and MAC address with your own. On Linux and macOS, use `arduino-cli board list` to find the serial port.

**1. Install the build dependencies.** The release was built with Arduino CLI 1.5.1 and the versions below.

```sh
arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core install esp32:esp32@3.3.12 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli lib update-index
arduino-cli lib install "Adafruit BusIO@1.17.4" "Adafruit GFX Library@1.13.0" "GxEPD2@1.6.9"
python -m pip install -r requirements.txt
```

**2. Build and install over USB.** The build script embeds the compressed web page and checks that the firmware image contains the same page. Uploading replaces the program on the board. Check the serial port and hardware first, and back up any board already in use. This build uses a dual-application partition layout and a dedicated album area.

```sh
python build.py
arduino-cli board list
arduino-cli upload -p COM5 --fqbn "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,USBMode=hwcdc,CDCOnBoot=cdc" --input-dir build firmware/simple_epaper
```

**If a previously used board reports “轮播存储不可用” (slideshow storage unavailable).** The album needs a blank `ffat` area in this partition layout. A normal USB upload preserves its contents. If the frame finds an unfamiliar format, it stops writing. Run `python panel.py info --port COM5` and check `hardware_ready` and `storage_ready`. To keep the old data, use a different, blank board. If you have backed up the board and want to dedicate it to the frame, append `,EraseFlash=all` to the FQBN in the USB upload command and flash again. **This erases the entire flash**, including old applications, photos, Wi-Fi settings and OTA keys. Pair the board and provision its keys again afterward.

**3. Pair your board.** Use `info` to read the MAC address from the newly flashed board and paste it into `--mac`. The terminal hides your Wi-Fi password as you enter it. The computer saves the selected board and a randomly generated access token.

```sh
python panel.py info --port COM5
python panel.py configure --port COM5 --ssid "Your-2.4GHz-WiFi" --mac "02:00:00:00:00:01"
python panel.py status
```

Connect your phone to the same network and open **`http://BOARD_IP:28463/`**, replacing `BOARD_IP` with the IP shown after pairing. If your device supports mDNS, you can also use the `host` returned by `info` with port `28463`. Anyone on the same subnet can view photos, send them or clear the slideshow. Use a trusted home network. Guest Wi-Fi with client isolation can prevent the phone from reaching the frame.

## Wireless firmware updates

Use USB once to provision a unique over-the-air (OTA) key for the board. After that you can update it over home Wi-Fi. Windows protects the key with DPAPI. On Unix the tool restricts permissions on the key file. The tools keep configuration and keys in your local user configuration directory. When pairing multiple boards, add `--config PATH` after `panel.py configure`. For the OTA tool, place it before the subcommand, as in `python ota.py --config PATH status`.

```sh
python ota.py provision --port COM5
python ota.py status
```

Release 1.0.0 uses the firmware ID `xiaoyi-frame-1`. To build a new firmware version, increment the ID in `firmware/simple_epaper/release.h` (for example, to `xiaoyi-frame-2`), then run the commands below. The updater checks board identity, the image and the trial boot partition. The board accepts an update when it is idle and the slideshow list is empty.

```sh
python build.py
python ota.py update build/simple_epaper.ino.bin xiaoyi-frame-2
```

## Preparing and sending photos from a script

```sh
python panel.py prepare-photo photo.jpg --output-dir prepared
python panel.py push-frame prepared/frame.bin --sha256 "FRAME_SHA256_FROM_PREPARE_PHOTO" --receipt receipt.json
```

Inspect `prepared/preview.png` first. To reframe the photo, rerun the first command with a new output directory and set `--center-x` and `--center-y` between 0 and 1. To send your preferred crop, use its `frame.bin` path and replace `FRAME_SHA256_FROM_PREPARE_PHOTO` with the corresponding `sha256` from the preparation command. The CLI uses the desktop six-color conversion. The web page runs its enhanced mode in the browser.

## Verification

The unit and browser tests use synthetic photos, a simulated board and a local HTTP server. You can run them without a physical display. The C++ tests simulate interrupted flash writes, reboots, changes in photo order and recovery.

```sh
python -m pip install -r requirements-dev.txt
python -m unittest -v test_panel test_pairing
python -m playwright install chromium webkit
python test_family_page.py --engine chromium
python test_family_page.py --engine webkit
g++ -std=c++17 -I tests/gallery_flash -I firmware/simple_epaper tests/gallery_flash/test_gallery_flash.cpp firmware/simple_epaper/gallery_flash.cpp -o gallery-test
./gallery-test
```

The family web page and slideshow logic come from a prototype running on the same hardware. The public version adds hardware detection and per-board USB pairing. Its firmware build and software tests passed. First installation, Wi-Fi setup, slideshow recovery after a physical power cut and OTA still need verification on a board running the public version.

## License

The application source is licensed under **GPL-3.0-only**. Install Arduino ESP32, GxEPD2, Adafruit GFX and BusIO through the package and library managers. Each dependency retains its upstream copyright and license. [Full license](LICENSE) · [Arduino ESP32](https://github.com/espressif/arduino-esp32) · [GxEPD2](https://github.com/ZinggJM/GxEPD2) · [Adafruit GFX](https://github.com/adafruit/Adafruit-GFX-Library) · [BusIO](https://github.com/adafruit/Adafruit_BusIO)

# 小艺六色墨水屏相框

**简体中文** · [English](README.en.md)

用一块 ESP32-S3 和一块六色墨水屏做家庭相框。家人连上同一 Wi-Fi，用手机浏览器选照片、裁剪、看六色预览，再投到屏上。网页由相框自己提供，播放列表保存在板载闪存。配好后可以关掉电脑，无需账号、云服务或投图码。

## 家里怎么用

| 想做什么 | 页面操作 |
| --- | --- |
| 看屏上是哪张 | 「当前画面」可放大最近一次确认刷新的 800×480 画面。播放中可看张数、间隔和下一张。 |
| 竖照片铺满横屏 | 默认按比例铺满。拖动、双指缩放，也可以点方向和缩放按钮。想保留整张时切到留白，每张可单独设置。 |
| 看照片转成六色的效果 | 在裁剪页点「看六色效果」。可逐张比较「层次增强」和「经典」。转换在浏览器本地完成。 |
| 投一组照片 | 选 2 至 12 张，设为每 15、30 或 60 分钟换图。提交前可以继续添加、跳转、排序和移出。 |
| 改已经在播的相册 | 勾选一两张替换，或直接整理现有顺序、调整间隔、整组换新、清空。其他人先改过时，页面会要求核对最新列表。 |
| 断电后继续播放 | 上电后从已保存的位置恢复。开机有 180 秒刷新保护。刷新失败时保留照片，在页面点「重试轮播」。 |

## 硬件和接线

构建配置针对 **ESP32-S3 N16R8**（16 MB Flash、8 MB OPI PSRAM）和 **Waveshare 7.3inch e-Paper HAT (E)** 六色 800×480 面板，使用 GDEP073E01 驱动。先核对面板后缀及自己的开发板引脚标识。板载闪存承担轮播存储，无需 SD 卡。

| HAT (E) | ESP32-S3 |
| --- | --- |
| VCC / GND | 3V3 / GND |
| DIN / SCLK | GPIO4 / GPIO5 |
| CS / DC | GPIO6 / GPIO7 |
| RST / BUSY | GPIO15 / GPIO16 |

[微雪面板手册](https://www.waveshare.com/wiki/7.3inch_e-Paper_HAT_%28E%29_Manual) · [GxEPD2 驱动](https://github.com/ZinggJM/GxEPD2)

## 第一次安装

下面从仓库根目录执行。准备 Python 3.11 或更新版本、Arduino CLI，以及自己的 USB 数据线。示例里的 `COM5`、Wi-Fi 名和设备 MAC 请换成实际值。Linux/macOS 的串口可从 `arduino-cli board list` 查看。

**1. 安装编译依赖。** 以下版本用于本次构建验证。Arduino CLI 测试版本为 1.5.1。

```sh
arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core install esp32:esp32@3.3.12 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli lib update-index
arduino-cli lib install "Adafruit BusIO@1.17.4" "Adafruit GFX Library@1.13.0" "GxEPD2@1.6.9"
python -m pip install -r requirements.txt
```

**2. 编译并通过 USB 安装。** `build.py` 会嵌入压缩网页，编译后检查最终镜像包含同一份网页。烧录会替换目标板当前程序，先核对串口和硬件。在已有其他用途的板子上请先备份。项目使用指定双应用分区和专用相册区域。

```sh
python build.py
arduino-cli board list
arduino-cli upload -p COM5 --fqbn "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,USBMode=hwcdc,CDCOnBoot=cdc" --input-dir build firmware/simple_epaper
```

**旧板提示「轮播存储不可用」时。** 轮播需要指定分区布局中的空白 `ffat` 区域。普通 USB 烧录会保留其中原有数据，小艺发现其他格式时会停止写入。先用 `panel.py info --port COM5` 核对 `hardware_ready` 和 `storage_ready`。需保留旧内容时，可改用另一块空白板。只有已备份旧板、准备将它专用于相框时，才在第 2 步上传命令的 FQBN 末尾追加 `,EraseFlash=all`，通过 USB 重刷。这个选项会擦除整块闪存，包括旧应用、照片、Wi-Fi 和 OTA 密钥。完成后重新配网并配置所需密钥。

**3. 配对自己的板子。** 先用 `info` 查看刚刷好固件的 MAC，再把这个地址填入 `--mac`。密码由终端隐藏输入。电脑配置保存选择的板子和随机生成的访问令牌。

```sh
python panel.py info --port COM5
python panel.py configure --port COM5 --ssid "Your-2.4GHz-WiFi" --mac "02:00:00:00:00:01"
python panel.py status
```

用手机连同一个局域网，在浏览器打开 **`http://设备IP:28463/`**。IP 见配网成功时的输出。支持 mDNS 的设备也可用 `info` 返回的 `host` 加端口 `28463`。同子网访问者都可以查看、投图及清空轮播，适合信任的家庭网络。访客网络开启客户端隔离时，手机可能无法连接相框。

## 后续无线升级

首次用 USB 设置本板独立的 OTA 密钥，随后可以通过家庭网络升级。Windows 使用 DPAPI 保存密钥，Unix 系统限制密钥文件权限。设备配置和密钥保存在本机用户配置目录。多板配对时，在 `panel.py configure` 后加 `--config 路径`。OTA 的配置参数放在子命令前，例如 `python ota.py --config 路径 status`。

```sh
python ota.py provision --port COM5
python ota.py status
```

发布版 1.0.0 对应固件标识 `xiaoyi-frame-1`。制作下一版时，在 `firmware/simple_epaper/release.h` 递增标识，例如 `xiaoyi-frame-2`，重新构建，然后执行下面的命令。更新器核对板子身份、镜像和试运行分区。板子在轮播列表为空且空闲时受理升级。

```sh
python build.py
python ota.py update build/simple_epaper.ino.bin xiaoyi-frame-2
```

## 可脚本化的图片处理

```sh
python panel.py prepare-photo photo.jpg --output-dir prepared
python panel.py push-frame prepared/frame.bin --sha256 "FRAME_SHA256_FROM_PREPARE_PHOTO" --receipt receipt.json
```

先看 `prepared/preview.png`。调整裁剪时，换一个新的输出目录，用 `--center-x`、`--center-y` 指定裁剪中心，范围为 0 到 1。发送时使用选中版本的 `frame.bin` 路径，并把 `FRAME_SHA256_FROM_PREPARE_PHOTO` 换成生成该版本时输出的 `sha256`。命令行使用桌面六色转换路径，网页的增强模式在浏览器中运行。

## 验证

单元测试与浏览器测试使用合成照片、模拟板端和本机回环 HTTP。下面的命令不需要实体屏。C++ 测试模拟闪存写入中断、重启、排序和恢复。

```sh
python -m pip install -r requirements-dev.txt
python -m unittest -v test_panel test_pairing
python -m playwright install chromium webkit
python test_family_page.py --engine chromium
python test_family_page.py --engine webkit
g++ -std=c++17 -I tests/gallery_flash -I firmware/simple_epaper tests/gallery_flash/test_gallery_flash.cpp firmware/simple_epaper/gallery_flash.cpp -o gallery-test
./gallery-test
```

本版本的家庭网页和播放逻辑来自同型号家用原型。开源版改用通用硬件识别和逐板 USB 配对，已验证源码编译及软件测试。开源版首次上板、实际配网、物理断电续播和 OTA 仍需在目标板验收。

## 许可证

项目源码采用 **GPL-3.0-only**。本仓库发布应用源码，Arduino ESP32、GxEPD2、Adafruit GFX 和 BusIO 通过库管理器安装，各自的版权和许可证见上游。[许可证全文](LICENSE) · [Arduino ESP32](https://github.com/espressif/arduino-esp32) · [GxEPD2](https://github.com/ZinggJM/GxEPD2) · [Adafruit GFX](https://github.com/adafruit/Adafruit-GFX-Library) · [BusIO](https://github.com/adafruit/Adafruit_BusIO)

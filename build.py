"""Compile the complete embedded web page and ESP32-S3 firmware with Arduino CLI."""
import argparse
import gzip
from hashlib import sha256
from pathlib import Path
import shutil
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parent
SKETCH = ROOT / "firmware" / "simple_epaper"
FQBN = "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,USBMode=hwcdc,CDCOnBoot=cdc"

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", default="arduino-cli")
    parser.add_argument("--config-file", type=Path, help="Optional Arduino CLI configuration")
    parser.add_argument("--output", type=Path, default=ROOT / "build")
    args = parser.parse_args()
    out = args.output.resolve()
    src = (SKETCH / "family_page.h").read_bytes()
    page = src.split(b'R"familyweb(', 1)[1].rsplit(b')familyweb"', 1)[0]
    packed = gzip.compress(page, mtime=0)
    cli = [args.cli]
    if args.config_file:
        cli += ["--config-file", str(args.config_file.resolve())]
    with tempfile.TemporaryDirectory() as temp:
        sketch = Path(temp) / SKETCH.name
        sketch.mkdir()
        for f in SKETCH.iterdir():
            if f.is_file() and f.suffix in (".h", ".cpp", ".ino"):
                shutil.copyfile(f, sketch / f.name)
        array = ",".join(f"0x{value:02x}" for value in packed)
        (sketch / "family_page_gzip.h").write_text(
            "constexpr unsigned char FamilyPageGzip[]={" + array + "};\n", encoding="ascii")
        subprocess.run(cli + ["compile","--fqbn",FQBN,"--output-dir",str(out),str(sketch)],check=True)
    image = out / "simple_epaper.ino.bin"
    data = image.read_bytes()
    if packed not in data or zlib.decompress(packed, wbits=31) != page:
        raise ValueError("The built firmware does not contain the tested web page")
    print(f"{image}: {len(data)} bytes, SHA256 {sha256(data).hexdigest()}")

if __name__ == "__main__":
    main()

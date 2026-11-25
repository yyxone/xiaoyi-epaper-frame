"""Six-color panel palette, fitting, quantization, and packed frame format."""
from PIL import Image, ImageOps

WIDTH, HEIGHT = 800, 480

# index: (name, RGB used for dithering and the preview)
# "measured": medians sampled from the photo of this panel showing the six bars
# (phone camera, indoor light),
# so it is an approximation of the real panel, not a calibrated value.
PALETTES = {
    "measured": [
        ("black", (67, 52, 57)),
        ("white", (200, 197, 182)),
        ("yellow", (216, 177, 48)),
        ("red", (161, 60, 34)),
        ("blue", (63, 89, 145)),
        ("green", (88, 93, 73)),
    ],
    "pure": [
        ("black", (0, 0, 0)),
        ("white", (255, 255, 255)),
        ("yellow", (255, 255, 0)),
        ("red", (255, 0, 0)),
        ("blue", (0, 0, 255)),
        ("green", (0, 255, 0)),
    ],
}
PALETTE = PALETTES["measured"]


def fit(img: Image.Image, mode: str, center_y: float, center_x: float = 0.5) -> Image.Image:
    if mode == "crop":
        return ImageOps.fit(img, (WIDTH, HEIGHT), Image.Resampling.LANCZOS, centering=(center_x, center_y))
    canvas = Image.new("RGB", (WIDTH, HEIGHT), (255, 255, 255))
    inner = ImageOps.contain(img, (WIDTH, HEIGHT), Image.Resampling.LANCZOS)
    canvas.paste(inner, ((WIDTH - inner.width) // 2, (HEIGHT - inner.height) // 2))
    return canvas


def quantize(img: Image.Image) -> list[int]:
    flat = [c for _, rgb in PALETTE for c in rgb]
    flat += list(PALETTE[0][1]) * (256 - len(PALETTE))
    pal_img = Image.new("P", (1, 1))
    pal_img.putpalette(flat)
    q = img.quantize(palette=pal_img, dither=Image.Dither.FLOYDSTEINBERG)
    # Padding entries duplicate black; fold any of them back to index 0.
    return [i if i < len(PALETTE) else 0 for i in q.get_flattened_data()]


def pack_indices(indices: list[int]) -> bytes:
    if len(indices) != WIDTH * HEIGHT:
        raise ValueError("Expected a complete 800x480 image")
    return bytes((indices[k] << 4) | indices[k + 1] for k in range(0, len(indices), 2))

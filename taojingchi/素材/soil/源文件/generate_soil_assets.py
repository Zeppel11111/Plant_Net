"""Generate static background and mock preview for the TJC soil page (1024x600)."""

from pathlib import Path
import math

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
SIZE = (1024, 600)
SCALE = 2
FONT_CN = Path(r"C:\Windows\Fonts\msyh.ttc")
FONT_CN_BOLD = Path(r"C:\Windows\Fonts\msyhbd.ttc")
FONT_NUM = Path(r"C:\Windows\Fonts\bahnschrift.ttf")

ACCENTS = ["#E0AF73", "#7EDBC5", "#B9D97A", "#C1A9E8", "#DCA878", "#94C98A", "#80C5A5", "#E5C66F"]
CARDS = [
    ("土壤温度", "SOIL TEMPERATURE", "°C", "thermo", "24.8"),
    ("土壤湿度", "SOIL MOISTURE", "%", "drop", "46.2"),
    ("电导率", "CONDUCTIVITY", "µS/cm", "ec", "1280"),
    ("酸碱度", "PH VALUE", "pH", "ph", "6.8"),
    ("土壤盐分", "SALINITY", "ppm", "salt", "620"),
    ("氮含量", "NITROGEN", "mg/kg", "n", "86"),
    ("磷含量", "PHOSPHORUS", "mg/kg", "p", "42"),
    ("钾含量", "POTASSIUM", "mg/kg", "k", "158"),
]


def px(value):
    if isinstance(value, tuple):
        return tuple(int(v * SCALE) for v in value)
    return int(value * SCALE)


def font(path, size):
    return ImageFont.truetype(str(path), size * SCALE)


def color(value):
    value = value.lstrip("#")
    return tuple(int(value[i:i + 2], 16) for i in (0, 2, 4))


def line(draw, points, fill, width=1):
    draw.line([px(point) for point in points], fill=fill, width=px(width), joint="curve")


def rect(draw, box, radius, fill, outline=None, width=1):
    draw.rounded_rectangle(px(box), radius=px(radius), fill=fill, outline=outline, width=px(width))


def text(draw, xy, value, size, fill, *, bold=False, numeric=False, anchor=None):
    path = FONT_NUM if numeric else FONT_CN_BOLD if bold else FONT_CN
    draw.text(px(xy), value, font=font(path, size), fill=fill, anchor=anchor)


def draw_icon(draw, kind, x, y, c):
    # Icons are drawn into the background so the HMI needs no separate bitmap resources.
    if kind == "thermo":
        draw.rounded_rectangle(px((x + 14, y + 3, x + 22, y + 28)), radius=px(4), outline=c, width=px(2))
        draw.ellipse(px((x + 9, y + 23, x + 27, y + 41)), outline=c, width=px(2))
        line(draw, [(x + 18, y + 12), (x + 18, y + 33)], c, 2)
        draw.ellipse(px((x + 14, y + 29, x + 22, y + 37)), fill=c)
    elif kind == "drop":
        pts = [(x + 18, y + 3), (x + 29, y + 22), (x + 31, y + 29), (x + 27, y + 37),
               (x + 18, y + 41), (x + 9, y + 37), (x + 5, y + 29), (x + 7, y + 22)]
        line(draw, pts + [pts[0]], c, 2)
        line(draw, [(x + 11, y + 29), (x + 15, y + 33)], c, 2)
    elif kind == "ec":
        pts = [(x + 23, y + 3), (x + 12, y + 23), (x + 20, y + 23), (x + 14, y + 39), (x + 29, y + 18), (x + 21, y + 18)]
        line(draw, pts, c, 2)
        draw.ellipse(px((x + 4, y + 4, x + 40, y + 40)), outline=c, width=px(1))
    elif kind == "ph":
        draw.ellipse(px((x + 4, y + 4, x + 40, y + 40)), outline=c, width=px(2))
        text(draw, (x + 22, y + 23), "pH", 13, c, numeric=True, anchor="mm")
    elif kind == "salt":
        pts = [(x + 18, y + 3), (x + 31, y + 10), (x + 31, y + 28), (x + 18, y + 36), (x + 5, y + 28), (x + 5, y + 10)]
        line(draw, pts + [pts[0]], c, 2)
        draw.ellipse(px((x + 15, y + 16, x + 21, y + 22)), fill=c)
    else:
        draw.ellipse(px((x + 4, y + 4, x + 40, y + 40)), outline=c, width=px(2))
        text(draw, (x + 22, y + 22), kind.upper(), 15, c, bold=True, numeric=True, anchor="mm")


def build(show_values):
    image = Image.new("RGB", px(SIZE), "#081512")
    pixels = image.load()
    for y in range(px(600)):
        fy = y / px(600)
        for x in range(px(1024)):
            fx = x / px(1024)
            glow = max(0.0, 1.0 - math.hypot(fx - 0.96, fy - 0.12) * 1.7)
            pixels[x, y] = (int(8 + 8 * glow), int(21 + 14 * glow), int(18 + 8 * glow))

    draw = ImageDraw.Draw(image)
    for x in range(0, 1025, 32):
        line(draw, [(x, 0), (x, 600)], "#10221e", 1)
    for y in range(0, 601, 32):
        line(draw, [(0, y), (1024, y)], "#10221e", 1)

    # Header matches the companion main/air pages and reserves a touch target for Back.
    rect(draw, (40, 34, 86, 80), 13, "#173126", "#355a43")
    line(draw, [(69, 47), (56, 57), (69, 67)], "#d7eaca", 2)
    text(draw, (108, 28), "土壤检测", 32, "#f0f5e9", bold=True)
    text(draw, (110, 70), "SOIL / SENSOR 01", 11, "#9bb7a4", numeric=True)
    rect(draw, (775, 39, 984, 77), 18, "#172b21", "#385541")
    draw.ellipse(px((792, 53, 801, 62)), fill="#b9dd88")
    text(draw, (811, 48), "探头地址 02  ·  RS485", 14, "#d7e9d2")
    line(draw, [(40, 105), (984, 105)], "#34483e")
    text(draw, (42, 116), "SOIL PROFILE / LIVE READINGS", 11, "#8ca598", numeric=True)
    text(draw, (982, 116), "8 MEASUREMENTS", 10, "#718d7d", numeric=True, anchor="ra")

    # Four columns by two rows; all eight fields from soil_data_t have a dedicated slot.
    xs = [40, 280, 520, 760]
    ys = [145, 333]
    for index, (label, english, unit, icon, sample) in enumerate(CARDS):
        x, y = xs[index % 4], ys[index // 4]
        accent = ACCENTS[index]
        rect(draw, (x, y, x + 224, y + 172), 16, "#14241d", "#34493b")
        rect(draw, (x + 16, y, x + 208, y + 4), 2, accent)
        draw_icon(draw, icon, x + 18, y + 17, accent)
        text(draw, (x + 67, y + 18), label, 18, "#e8efe6", bold=True)
        text(draw, (x + 68, y + 44), english, 9, "#91a597", numeric=True)
        line(draw, [(x + 18, y + 72), (x + 206, y + 72)], "#30443a")
        if show_values:
            value = sample
            value_size = 45 if len(value) <= 4 else 39
            text(draw, (x + 20, y + 82), value, value_size, "#f2f5e9", numeric=True)
        text(draw, (x + 20, y + 141), unit, 12, accent, numeric=True)

    line(draw, [(40, 535), (984, 535)], "#34483e")
    text(draw, (40, 552), "PLANT NET   /   LOCAL SOIL MONITOR", 10, "#799184", numeric=True)
    text(draw, (512, 552), "Modbus RTU   ·   9600 / 8N1", 10, "#799184", numeric=True, anchor="mm")
    text(draw, (984, 552), "SOIL  ·  01", 10, "#799184", numeric=True, anchor="ra")
    return image.resize(SIZE, Image.Resampling.LANCZOS)


def main():
    background = ROOT / "背景" / "soil_bg_1024x600.png"
    preview = ROOT / "预览" / "soil_preview_1024x600.png"
    background.parent.mkdir(parents=True, exist_ok=True)
    preview.parent.mkdir(parents=True, exist_ok=True)
    build(False).save(background, optimize=True)
    build(True).save(preview, optimize=True)
    print(background)
    print(preview)


if __name__ == "__main__":
    main()

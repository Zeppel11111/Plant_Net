"""Generate the TJC two-sensor soil comparison background and sample preview."""

from pathlib import Path
import math

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
SIZE = (1024, 600)
SCALE = 2
FONT_CN = Path(r"C:\Windows\Fonts\msyh.ttc")
FONT_CN_BOLD = Path(r"C:\Windows\Fonts\msyhbd.ttc")
FONT_NUM = Path(r"C:\Windows\Fonts\bahnschrift.ttf")

ROWS = [
    ("土壤温度", "TEMPERATURE", "°C", "24.8", "25.1"),
    ("土壤湿度", "MOISTURE", "%", "46.2", "39.8"),
    ("电导率", "CONDUCTIVITY", "µS/cm", "1280", "1100"),
    ("酸碱度", "PH VALUE", "pH", "6.8", "7.1"),
    ("土壤盐分", "SALINITY", "ppm", "620", "540"),
    ("氮含量", "NITROGEN", "mg/kg", "86", "79"),
    ("磷含量", "PHOSPHORUS", "mg/kg", "42", "38"),
    ("钾含量", "POTASSIUM", "mg/kg", "158", "142"),
]
SYMBOLS = ["°C", "%", "EC", "pH", "S", "N", "P", "K"]


def scaled(value):
    if isinstance(value, tuple):
        return tuple(round(v * SCALE) for v in value)
    return round(value * SCALE)


def font(size, bold=False, numeric=False):
    path = FONT_NUM if numeric else FONT_CN_BOLD if bold else FONT_CN
    return ImageFont.truetype(str(path), scaled(size))


def label(draw, xy, value, size, fill, *, bold=False, numeric=False, anchor=None):
    draw.text(scaled(xy), value, font=font(size, bold, numeric), fill=fill, anchor=anchor)


def line(draw, xy, fill, width=1):
    draw.line([scaled(point) for point in xy], fill=fill, width=scaled(width))


def rounded(draw, box, radius, fill, outline=None, width=1):
    draw.rounded_rectangle(scaled(box), radius=scaled(radius), fill=fill,
                           outline=outline, width=scaled(width))


def make(show_values):
    image = Image.new("RGB", scaled(SIZE), "#081811")
    pixels = image.load()
    for y in range(scaled(600)):
        fy = y / scaled(600)
        for x in range(scaled(1024)):
            fx = x / scaled(1024)
            glow = max(0, 1 - math.hypot(fx - .53, fy + .18) * 1.6)
            pixels[x, y] = (round(8 + glow * 7), round(24 + glow * 13), round(17 + glow * 8))

    draw = ImageDraw.Draw(image)
    for x in range(0, 1025, 32):
        line(draw, [(x, 0), (x, 600)], "#11241a")
    for y in range(0, 601, 32):
        line(draw, [(0, y), (1024, y)], "#11241a")

    # Header and back area follow the established main/air/soil page language.
    rounded(draw, (40, 33, 86, 79), 14, "#173126", "#3e6651")
    line(draw, [(69, 47), (55, 56), (69, 66)], "#a9e8cd", 2)
    label(draw, (108, 27), "土壤对照", 30, "#f0f5e9", bold=True)
    label(draw, (110, 68), "SOIL COMPARISON / LIVE READINGS", 10, "#94ad9d", numeric=True)
    rounded(draw, (787, 39, 984, 76), 18, "#172d21", "#3b5943")
    draw.ellipse(scaled((803, 54, 811, 62)), fill="#82dcbc")
    label(draw, (825, 46), "2 SENSORS  ·  8 METRICS", 12, "#d6e7d3", numeric=True)
    line(draw, [(40, 104), (984, 104)], "#314b3b")
    label(draw, (42, 115), "MEASUREMENTS / PARAMETER COMPARISON", 10, "#88a08e", numeric=True)
    label(draw, (982, 115), "MODBUS RTU", 10, "#769380", numeric=True, anchor="ra")

    # Eight rows align identical metrics for direct comparison.
    rounded(draw, (40, 137, 984, 531), 15, "#10231b", "#345241")
    draw.rectangle(scaled((41, 138, 983, 187)), fill="#1b392a")
    line(draw, [(40, 187), (984, 187)], "#496653")
    line(draw, [(264, 138), (264, 531)], "#31483a")
    line(draw, [(624, 138), (624, 531)], "#31483a")
    label(draw, (60, 156), "指标 / PARAMETER", 13, "#afc4ae")
    rounded(draw, (332, 149, 362, 177), 8, "#254b3b", "#5c9a79")
    label(draw, (347, 163), "01", 12, "#97e6bf", numeric=True, anchor="mm")
    label(draw, (374, 151), "土壤传感器 1", 16, "#b6f1d5", bold=True)
    label(draw, (548, 157), "地址 02", 11, "#8fb09a")
    rounded(draw, (692, 149, 722, 177), 8, "#4e422c", "#9e7c48")
    label(draw, (707, 163), "02", 12, "#f3d38e", numeric=True, anchor="mm")
    label(draw, (734, 151), "土壤传感器 2", 16, "#f2d798", bold=True)
    label(draw, (908, 157), "地址 03", 11, "#bdac84")

    for index, (cn, en, unit, first, second) in enumerate(ROWS):
        top = 187 + index * 43
        bottom = top + 43
        if index % 2:
            draw.rectangle(scaled((42, top + 1, 982, bottom - 1)), fill="#12261d")
        if index < 7:
            line(draw, [(40, bottom), (984, bottom)], "#2a4033")
        rounded(draw, (59, top + 9, 84, top + 34), 7, "#1a3629", "#41614a")
        label(draw, (71, top + 21), SYMBOLS[index], 9 if index < 4 else 12,
              "#a9d6af", numeric=True, anchor="mm")
        label(draw, (97, top + 6), cn, 14, "#e3ece0", bold=True)
        label(draw, (97, top + 26), en, 8, "#7f9a85", numeric=True)

        # TJC text objects occupy x=330/690, y=top+1, w=150, h=41.
        label(draw, (497, top + 14), unit, 11, "#94aa96", numeric=True)
        label(draw, (857, top + 14), unit, 11, "#a99d7e", numeric=True)
        if show_values:
            label(draw, (480, top + 3), first, 30, "#e5f1df", numeric=True, anchor="ra")
            label(draw, (840, top + 3), second, 30, "#f3ead0", numeric=True, anchor="ra")

    line(draw, [(40, 544), (984, 544)], "#304c3c")
    label(draw, (40, 555), "PLANT NET  /  LOCAL SOIL MONITOR", 10, "#829b88", numeric=True)
    label(draw, (512, 555), "9600 / 8N1", 10, "#829b88", numeric=True, anchor="mt")
    label(draw, (984, 555), "SOIL  ·  01 + 02", 10, "#829b88", numeric=True, anchor="ra")
    return image.resize(SIZE, Image.Resampling.LANCZOS)


def main():
    background = ROOT / "背景" / "soil_compare_bg_1024x600.png"
    preview = ROOT / "预览" / "soil_compare_preview_1024x600.png"
    background.parent.mkdir(parents=True, exist_ok=True)
    preview.parent.mkdir(parents=True, exist_ok=True)
    make(False).save(background, optimize=True)
    make(True).save(preview, optimize=True)
    print(background)
    print(preview)


if __name__ == "__main__":
    main()

"""生成陶晶驰 air 页的静态背景与演示预览（1024×600）。"""

from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import math


ROOT = Path(__file__).resolve().parents[1]
SIZE = (1024, 600)
SCALE = 2
FONT_CN = Path(r"C:\Windows\Fonts\msyh.ttc")
FONT_CN_BOLD = Path(r"C:\Windows\Fonts\msyhbd.ttc")
FONT_NUM = Path(r"C:\Windows\Fonts\bahnschrift.ttf")


def font(path, size):
    return ImageFont.truetype(str(path), size * SCALE)


def pt(value):
    if isinstance(value, tuple):
        return tuple(int(v * SCALE) for v in value)
    return int(value * SCALE)


def rgba(hex_color, alpha=255):
    value = hex_color.lstrip("#")
    return (*tuple(int(value[i : i + 2], 16) for i in (0, 2, 4)), alpha)


def round_rect(draw, box, radius, fill, outline=None, width=1):
    draw.rounded_rectangle(pt(box), radius=pt(radius), fill=fill, outline=outline, width=pt(width))


def line(draw, points, color, width=1):
    draw.line([pt(p) for p in points], fill=color, width=pt(width), joint="curve")


def txt(draw, xy, value, size, color, bold=False, numeric=False, anchor=None):
    chosen = FONT_NUM if numeric else FONT_CN_BOLD if bold else FONT_CN
    draw.text(pt(xy), value, font=font(chosen, size), fill=color, anchor=anchor)


def thermometer(draw, x, y, color):
    s = SCALE
    draw.rounded_rectangle(pt((x + 11, y + 4, x + 21, y + 31)), radius=pt(5), outline=color, width=pt(2))
    draw.ellipse(pt((x + 6, y + 25, x + 26, y + 45)), outline=color, width=pt(2))
    line(draw, [(x + 16, y + 13), (x + 16, y + 35)], color, 3)
    draw.ellipse(pt((x + 12, y + 31, x + 20, y + 39)), fill=color)


def drop(draw, x, y, color):
    points = [(x + 18, y + 3), (x + 30, y + 23), (x + 32, y + 30),
              (x + 28, y + 40), (x + 18, y + 45), (x + 8, y + 40),
              (x + 4, y + 30), (x + 6, y + 23)]
    draw.line([pt(p) for p in points + [points[0]]], fill=color, width=pt(2), joint="curve")
    draw.arc(pt((x + 10, y + 23, x + 26, y + 39)), 55, 125, fill=color, width=pt(2))


def sun(draw, x, y, color):
    draw.ellipse(pt((x + 12, y + 12, x + 32, y + 32)), outline=color, width=pt(2))
    for i in range(8):
        a = math.tau * i / 8
        p1 = (x + 22 + math.cos(a) * 15, y + 22 + math.sin(a) * 15)
        p2 = (x + 22 + math.cos(a) * 21, y + 22 + math.sin(a) * 21)
        line(draw, [p1, p2], color, 2)


def build(show_values):
    image = Image.new("RGB", pt(SIZE), "#081512")
    px = image.load()
    for y in range(pt(600)):
        for x in range(pt(1024)):
            vx = x / pt(1024)
            vy = y / pt(600)
            glow = max(0.0, 1.0 - math.hypot(vx - 0.93, vy - 0.08) * 1.8)
            px[x, y] = (int(8 + glow * 7), int(21 + glow * 14), int(18 + glow * 9))

    draw = ImageDraw.Draw(image)
    for x in range(0, 1025, 32):
        line(draw, [(x, 0), (x, 600)], "#10221e", 1)
    for y in range(0, 601, 32):
        line(draw, [(0, y), (1024, y)], "#10221e", 1)

    # 页头：左侧留出返回按钮的热区；页面名与已有主屏风格保持一致。
    round_rect(draw, (40, 35, 86, 81), 13, "#173126", "#355a43")
    line(draw, [(69, 48), (56, 58), (69, 68)], "#d7eaca", 2)
    txt(draw, (108, 29), "空气环境", 33, "#f0f5e9", bold=True)
    txt(draw, (110, 73), "AIR / TASTEK SENSOR", 11, "#9bb7a4", numeric=True)
    round_rect(draw, (775, 41, 984, 78), 18, "#172b21", "#385541")
    draw.ellipse(pt((792, 55, 801, 64)), fill="#b9dd88")
    txt(draw, (811, 50), "塔石传感器  ·  RS485", 14, "#d7e9d2")
    line(draw, [(40, 112), (984, 112)], "#34483e")

    # 两块主数据区。背景图中不烘焙数值，供陶晶驰文本控件实时覆盖。
    round_rect(draw, (40, 143, 500, 448), 19, "#15261f", "#34493b")
    round_rect(draw, (524, 143, 984, 448), 19, "#142621", "#344b44")
    round_rect(draw, (40, 143, 500, 151), 4, "#ddaa6e")
    round_rect(draw, (524, 143, 984, 151), 4, "#69cdbf")

    thermometer(draw, 72, 177, "#f1bc7a")
    txt(draw, (121, 181), "空气温度", 21, "#e8efe6", bold=True)
    txt(draw, (122, 214), "AIR TEMPERATURE", 11, "#9baf9f", numeric=True)
    drop(draw, 559, 177, "#83e0cf")
    txt(draw, (608, 181), "空气湿度", 21, "#e8efe6", bold=True)
    txt(draw, (609, 214), "RELATIVE HUMIDITY", 11, "#9baf9f", numeric=True)

    txt(draw, (409, 299), "°C", 27, "#f1bc7a", numeric=True)
    txt(draw, (884, 299), "%RH", 24, "#83e0cf", numeric=True)
    line(draw, [(72, 374), (468, 374)], "#354a3e")
    line(draw, [(556, 374), (952, 374)], "#36534a")
    txt(draw, (73, 391), "温度 / 实时采集", 14, "#a6b8a8")
    txt(draw, (557, 391), "湿度 / 实时采集", 14, "#a6b8a8")
    txt(draw, (468, 390), "01", 14, "#789487", numeric=True, anchor="ra")
    txt(draw, (952, 390), "02", 14, "#789487", numeric=True, anchor="ra")

    # 光照为辅，独立的细卡片不会挤压温湿度读数。
    round_rect(draw, (40, 470, 984, 553), 18, "#182820", "#3d513a")
    sun(draw, 72, 489, "#d6e88e")
    txt(draw, (126, 483), "光照强度", 20, "#e8efe6", bold=True)
    txt(draw, (128, 516), "ILLUMINANCE / TASTEK", 11, "#9fb19b", numeric=True)
    line(draw, [(575, 489), (575, 535)], "#465a45")
    txt(draw, (906, 493), "lux", 22, "#d6e88e", numeric=True)

    txt(draw, (40, 570), "PLANT NET     /     LOCAL MONITOR", 10, "#799184", numeric=True)
    txt(draw, (984, 570), "AIR  ·  01", 10, "#799184", numeric=True, anchor="ra")

    if show_values:
        # 与目标文本控件的样例数据一致，仅用于查看设计稿。
        txt(draw, (76, 262), "23.6", 76, "#f5f6ec", numeric=True)
        txt(draw, (560, 262), "58.4", 76, "#f5f6ec", numeric=True)
        txt(draw, (694, 481), "1280", 54, "#f5f6ec", numeric=True)

    return image.resize(SIZE, Image.Resampling.LANCZOS)


def main():
    background = ROOT / "背景" / "air_bg_1024x600.png"
    preview = ROOT / "预览" / "air_preview_1024x600.png"
    background.parent.mkdir(parents=True, exist_ok=True)
    preview.parent.mkdir(parents=True, exist_ok=True)
    build(False).save(background, optimize=True)
    build(True).save(preview, optimize=True)
    print(background)
    print(preview)


if __name__ == "__main__":
    main()

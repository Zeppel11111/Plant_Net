"""生成陶晶驰 main 应用首页背景与预览（1024×600）。"""

from pathlib import Path
import math
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "air" / "源文件"))
from generate_air_assets import SIZE, line, pt, round_rect, txt  # noqa: E402


ROOT = Path(__file__).resolve().parents[1]


def leaf(draw, x, y):
    draw.ellipse(pt((x, y, x + 42, y + 42)), fill="#24402d", outline="#527751", width=pt(1))
    draw.polygon([pt((x + 10, y + 29)), pt((x + 17, y + 12)), pt((x + 33, y + 10)),
                  pt((x + 29, y + 26)), pt((x + 16, y + 32))], fill="#b9df91")
    line(draw, [(x + 13, y + 31), (x + 30, y + 14)], "#315744", 2)


def app_tile(draw, x, title, english, state, color, active, icon):
    y = 232
    fill = "#1b3124" if active else "#17251f"
    outline = "#79a86a" if active else "#394940"
    round_rect(draw, (x, y, x + 216, y + 255), 24, fill, outline, 2 if active else 1)
    if active:
        round_rect(draw, (x + 2, y + 2, x + 214, y + 8), 3, color)
    round_rect(draw, (x + 22, y + 26, x + 108, y + 112), 21,
               "#335840" if active else "#23382f", "#547459" if active else "#3b5147")
    icon(draw, x + 43, y + 46, color)
    txt(draw, (x + 23, y + 137), title, 22, "#f0f4ea" if active else "#cfddd2", bold=True)
    txt(draw, (x + 25, y + 176), english, 11, "#a8c7a8" if active else "#889d90", numeric=True)
    line(draw, [(x + 23, y + 208), (x + 193, y + 208)], "#55705a" if active else "#354a3d")
    draw.ellipse(pt((x + 25, y + 226, x + 31, y + 232)), fill=color if active else "#788b7e")
    txt(draw, (x + 43, y + 216), state, 12, "#cde5bd" if active else "#899e90")
    if active:
        line(draw, [(x + 175, y + 229), (x + 193, y + 229)], color, 2)
        line(draw, [(x + 187, y + 223), (x + 193, y + 229), (x + 187, y + 235)], color, 2)


def air_icon(draw, x, y, color):
    for top, start, end in [(7, 0, 34), (25, 8, 45), (43, 0, 36)]:
        line(draw, [(x + start, y + top), (x + end, y + top)], color, 3)
    draw.arc(pt((x + 25, y + 2, x + 58, y + 31)), 280, 105, fill=color, width=pt(3))
    draw.arc(pt((x + 33, y + 20, x + 61, y + 48)), 278, 95, fill=color, width=pt(3))
    draw.polygon([pt((x + 40, y + 51)), pt((x + 59, y + 45)), pt((x + 54, y + 59))], fill=color)


def soil_icon(draw, x, y, color):
    line(draw, [(x + 2, y + 53), (x + 59, y + 53)], color, 3)
    line(draw, [(x + 31, y + 51), (x + 31, y + 17)], color, 3)
    draw.ellipse(pt((x + 7, y + 8, x + 33, y + 29)), outline=color, width=pt(3))
    draw.ellipse(pt((x + 29, y + 1, x + 55, y + 23)), outline=color, width=pt(3))
    for a in (12, 28, 45):
        line(draw, [(x + a, y + 60), (x + a + 6, y + 65)], color, 2)


def network_icon(draw, x, y, color):
    draw.arc(pt((x + 1, y + 1, x + 62, y + 64)), 210, 330, fill=color, width=pt(3))
    draw.arc(pt((x + 12, y + 14, x + 51, y + 54)), 210, 330, fill=color, width=pt(3))
    draw.arc(pt((x + 23, y + 27, x + 40, y + 44)), 210, 330, fill=color, width=pt(3))
    draw.ellipse(pt((x + 27, y + 51, x + 36, y + 60)), fill=color)


def settings_icon(draw, x, y, color):
    cx, cy = x + 31, y + 31
    for i in range(8):
        a = math.tau * i / 8
        p1 = (cx + math.cos(a) * 24, cy + math.sin(a) * 24)
        p2 = (cx + math.cos(a) * 33, cy + math.sin(a) * 33)
        line(draw, [p1, p2], color, 5)
    draw.ellipse(pt((x + 9, y + 9, x + 53, y + 53)), outline=color, width=pt(4))
    draw.ellipse(pt((x + 24, y + 24, x + 38, y + 38)), outline=color, width=pt(4))


def build():
    image = Image.new("RGB", pt(SIZE), "#081512")
    px = image.load()
    for y in range(pt(600)):
        for x in range(pt(1024)):
            vx = x / pt(1024)
            vy = y / pt(600)
            glow = max(0.0, 1.0 - math.hypot(vx - 0.55, vy - 0.55) * 1.8)
            px[x, y] = (int(7 + glow * 7), int(21 + glow * 15), int(17 + glow * 9))
    draw = ImageDraw.Draw(image)
    for x in range(0, 1025, 40):
        line(draw, [(x, 0), (x, 600)], "#10221e")
    for y in range(0, 601, 40):
        line(draw, [(0, y), (1024, y)], "#10221e")

    leaf(draw, 42, 37)
    txt(draw, (100, 28), "Plant Net", 31, "#f1f5ea", bold=True)
    txt(draw, (101, 70), "植物环境监测  /  LOCAL HOME", 12, "#98b29e")
    txt(draw, (983, 45), "ESP32-S3  ·  LOCAL HMI", 12, "#8fa798", numeric=True, anchor="ra")
    line(draw, [(42, 105), (983, 105)], "#34483e")

    txt(draw, (44, 129), "我的应用", 34, "#edf4e9", bold=True)
    txt(draw, (45, 181), "选择功能模块", 15, "#a9bda9")
    txt(draw, (982, 184), "ACTIVE MODULES  /  02", 11, "#7c9685", numeric=True, anchor="ra")

    app_tile(draw, 44, "空气环境", "AIR MONITOR", "打开应用", "#b9e59b", True, air_icon)
    app_tile(draw, 284, "土壤检测", "SOIL MONITOR", "打开应用", "#8bc8aa", True, soil_icon)
    app_tile(draw, 524, "数据上行", "NETWORK", "规划中", "#89b5cc", False, network_icon)
    app_tile(draw, 764, "系统设置", "SETTINGS", "规划中", "#d0b898", False, settings_icon)

    line(draw, [(44, 523), (980, 523)], "#30443a")
    txt(draw, (45, 539), "轻触已开放的应用进入对应页面", 12, "#91a999")
    txt(draw, (981, 540), "HOME  ·  01", 11, "#7a9482", numeric=True, anchor="ra")
    return image.resize(SIZE, Image.Resampling.LANCZOS)


def main():
    background = ROOT / "背景" / "main_bg_1024x600.png"
    preview = ROOT / "预览" / "main_preview_1024x600.png"
    background.parent.mkdir(parents=True, exist_ok=True)
    preview.parent.mkdir(parents=True, exist_ok=True)
    output = build()
    output.save(background, optimize=True)
    output.save(preview, optimize=True)
    print(background)
    print(preview)


if __name__ == "__main__":
    main()

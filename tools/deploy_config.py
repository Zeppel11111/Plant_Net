#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Plant_Net 统一配网脚本（单一数据源）
===================================
唯一配置源头：config/net_config.h
改法：
  1) 交互/参数模式：本脚本改写源头并同步两份编译副本
  2) --sync：只把源头同步到两份副本（适合记事本直接改源头后同步）
编译副本（必须保持一致，否则烧录的不生效）：
  STM32/Core/Inc/net_config.h
  ESP-CAM/CameraWebServer/net_config.h

用法：
  python tools/deploy_config.py                       # 交互填写
  python tools/deploy_config.py --ssid ABC --pass x --ip 192.168.137.1
  python tools/deploy_config.py --sync                # 手动改源头后同步
  python tools/deploy_config.py --dry-run --ssid ABC  # 只预览
"""
import argparse
import re
import shutil
import sys
from pathlib import Path

BASE = Path(__file__).resolve().parent.parent
CANON = BASE / "config/net_config.h"
CANON_TEMPLATE = BASE / "config/net_config.example.h"
COPIES = [
    BASE / "STM32/Core/Inc/net_config.h",
    BASE / "ESP-CAM/CameraWebServer/net_config.h",
]

FIELDS = [  # (宏名, 是否数字, 提示)
    ("PN_WIFI_SSID", False, "WiFi SSID（两块板共用）"),
    ("PN_WIFI_PASS", False, "WiFi 密码"),
    ("PN_SERVER_HOST", False, "服务器地址（绑定=192.168.137.1；公网=cpolar 域名）"),
    ("PN_DATA_PORT", True, "数据 TCP 端口（默认 9000）"),
    ("PN_PUSH_PORT", True, "推流 TCP 端口（默认 9001）"),
]


def read_text(p: Path) -> str:
    return p.read_text(encoding="utf-8", newline="")


def write_text(p: Path, data: str) -> None:
    with open(p, "w", encoding="utf-8", newline="") as f:
        f.write(data)


def ensure_canon() -> None:
    """首次克隆公开仓库时，从无凭据模板生成本地配置。"""
    if CANON.exists():
        return
    if not CANON_TEMPLATE.exists():
        print(f"缺少配置模板: {CANON_TEMPLATE}")
        sys.exit(1)
    shutil.copyfile(CANON_TEMPLATE, CANON)
    print(f"[OK] 已从模板生成本地配置 -> {CANON}")
    print("[!!] 请填写 PN_WIFI_SSID 与 PN_WIFI_PASS 后重新运行本脚本。")


def grep_value(macro: str, is_int: bool) -> str:
    if is_int:
        m = re.search(r'#define\s+%s\s+(\d+)' % macro, read_text(CANON))
    else:
        m = re.search(r'#define\s+%s\s+"([^"]*)"' % macro, read_text(CANON))
    return m.group(1) if m else "?"


def set_macro(data: str, macro: str, is_int: bool, value: str) -> str:
    if is_int:
        return re.sub(r'(#define\s+%s\s+)\d+' % macro,
                      r'\g<1>%s' % value, data, count=1)
    return re.sub(r'(#define\s+%s\s+)"[^"]*"' % macro,
                  r'\g<1>"%s"' % value, data, count=1)


def show_current():
    print("== 当前 config/net_config.h 值（回车保留）==")
    for macro, is_int, _ in FIELDS:
        print(f"  {macro:<16}: {grep_value(macro, is_int)}")


def sync_copies():
    for p in COPIES:
        if not p.parent.exists():
            print(f"[!!] 目标目录不存在: {p.parent}"); sys.exit(1)
        shutil.copyfile(CANON, p)
        print(f"[OK] 已同步 -> {p}")


def main():
    ap = argparse.ArgumentParser(description="Plant_Net 统一配网（只改 net_config.h）")
    ap.add_argument("--ssid", help="WiFi SSID")
    ap.add_argument("--pass", dest="pwd", help="WiFi 密码")
    ap.add_argument("--ip", help="服务器地址(主机名或 IP)")
    ap.add_argument("--dport", type=int, help="数据端口(默认9000)")
    ap.add_argument("--pport", type=int, help="推流端口(默认9001)")
    ap.add_argument("--sync", action="store_true", help="只把源头同步到副本，不改值")
    ap.add_argument("--dry-run", action="store_true", help="只预览不写入")
    args = ap.parse_args()

    ensure_canon()

    if args.sync:
        sync_copies()
        print("同步完成。之后分别重新编译烧录 STM32 与 ESP-CAM 即可。")
        return

    show_current()
    vals = {}
    for macro, is_int, prompt in FIELDS:
        cur = grep_value(macro, is_int)
        argmap = {"PN_WIFI_SSID": args.ssid, "PN_WIFI_PASS": args.pwd,
                  "PN_SERVER_HOST": args.ip,
                  "PN_DATA_PORT": str(args.dport) if args.dport else None,
                  "PN_PUSH_PORT": str(args.pport) if args.pport else None}
        v = argmap[macro]
        if v is None:
            v = input(f"{prompt} [{cur}] > ").strip() or cur
        vals[macro] = v
    if not all(vals.values()):
        print("配置值不能为空"); sys.exit(1)

    print("\n== 写入与同步 ==")
    data = read_text(CANON)
    for macro, is_int, _ in FIELDS:
        value = vals[macro]
        old = grep_value(macro, is_int)
        data = set_macro(data, macro, is_int, value)
        flag = "预览" if args.dry_run else "写入"
        print(f"  [{flag}] {macro}: {old} -> {value}")

    if not args.dry_run:
        write_text(CANON, data)
        sync_copies()
        print("\n== 完成 ==")
        print("  改动生效位置：")
        for p in COPIES:
            print(f"    {p}")
        print("  下一步：STM32 重新编译烧录；ESP-CAM 用 Arduino 重新上传。")
    else:
        print("\n[dry-run] 未写入任何文件。")


if __name__ == "__main__":
    main()

"""相机接入：接收 JPEG 帧、维护最新画面，并保存定时或手动截图。"""

import os
import socket
import threading
import time
from datetime import datetime
from .config import PHOTO_DIR, PUSH_PORT, RECORD_DEVICE, SNAP_INTERVAL_SEC
from .device_ids import AIR_DEVICE_ID
from .storage import get_conn


SNAP_PORT = 80        # CAM 相机服务端口（例程自带 httpd）


SNAP_PATH = "/capture"   # 取单帧路径（例程自带）；监控流同端口 /stream


STREAM_PORT = 80


CAM_IP = {}


FRAME_LOCK = threading.Lock()


FRAME_BUF = {"jpeg": None, "ts": 0.0, "ok": 0, "bad": 0}


def photo_save(device_id, temp_x10, humi_x10, lux, jpeg):
    """JPEG 字节落盘 + 路径入库。返回相对路径。出错只打印，不崩服务器。"""
    name = datetime.now().strftime("%Y%m%d_%H%M%S_%f") + ".jpg"
    os.makedirs(PHOTO_DIR, exist_ok=True)
    path = os.path.join(PHOTO_DIR, name)
    with open(path, "wb") as f:
        f.write(jpeg)
    rel = "photos/" + name
    conn = get_conn()
    try:
        with conn.cursor() as cur:
            cur.execute(
                "INSERT INTO photo_records (device_id, image_path, temp_x10, humi_x10, lux) "
                "VALUES (%s, %s, %s, %s, %s)",
                (device_id, rel, temp_x10, humi_x10, lux))
        conn.commit()
    except Exception as e:
        print("[错误] 照片入库失败:", e)
    finally:
        conn.close()
    return rel


def do_snap(dev):
    """拉 CAM 一帧 → 落盘 photos/ → photo_records。
    成功返回 (rel, None)；失败返回 (None, 错误描述)。"""
    ip = CAM_IP.get(dev)
    if not ip:
        return None, "还没收到 %s 的数据，学不到 CAM 的 IP" % dev

    import urllib.request
    url = "http://%s:%d%s" % (ip, SNAP_PORT, SNAP_PATH)
    try:
        with urllib.request.urlopen(url, timeout=8) as resp:
            jpeg = resp.read()
    except Exception as e:
        return None, "拉图失败: %s" % e
    if not jpeg:
        return None, "CAM 返回空"

    # 附带最近一条读数做快照标记
    t = h = lux = -1
    conn = get_conn()
    try:
        with conn.cursor() as cur:
            cur.execute("SELECT temp_x10, humi_x10, lux FROM sensor_data "
                        "WHERE device_id=%s AND temp_x10 IS NOT NULL "
                        "AND humi_x10 IS NOT NULL AND lux IS NOT NULL "
                        "ORDER BY id DESC LIMIT 1", (AIR_DEVICE_ID,))
            row = cur.fetchone()
            t, h, lux = photo_air_snapshot(row)
    except Exception:
        pass
    finally:
        conn.close()

    rel = photo_save(dev, t, h, lux, jpeg)
    print("[截图] %s -> %s  (读数 t=%d h=%d lux=%d)" % (dev, rel, t, h, lux))
    return rel, None


def push_listener():
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", PUSH_PORT))
    srv.listen(2)
    print("[OK] 收流服务已启动，端口:", PUSH_PORT)
    while True:
        conn, addr = srv.accept()
        CAM_IP[RECORD_DEVICE] = addr[0]     # 从推流连接学到 CAM IP
        threading.Thread(target=_push_conn, args=(conn, addr), daemon=True).start()


def _push_conn(conn, addr):
    conn.settimeout(60)
    try:
        f = conn.makefile("rb")
        while True:
            line = f.readline()
            if not line:
                break
            if not line.startswith(b"L:"):
                continue
            n = int(line[2:].strip())
            data = f.read(n)
            if len(data) != n:
                break
            ok = n > 1000 and data[:2] == b"\xff\xd8"
            with FRAME_LOCK:
                if ok:
                    FRAME_BUF["jpeg"] = data
                    FRAME_BUF["ts"] = time.time()
                    FRAME_BUF["ok"] += 1
                else:
                    FRAME_BUF["bad"] += 1
    except Exception:
        pass
    finally:
        try:
            conn.close()
        except Exception:
            pass


def get_latest_frame():
    with FRAME_LOCK:
        return FRAME_BUF["jpeg"], FRAME_BUF["ts"]


def save_snapshot(dev=RECORD_DEVICE):
    """从最新推流帧截取一张：无帧/空帧返回 None（由调用方重试或报错）"""
    jpeg, ts = get_latest_frame()
    if not jpeg:
        print("[截图] 暂无可存帧（推流未连接或全空帧）")
        return None
    # 附带塔石设备最近一条有效空气读数。
    t = h = lux = -1
    conn = get_conn()
    try:
        with conn.cursor() as cur:
            cur.execute("SELECT temp_x10, humi_x10, lux FROM sensor_data "
                        "WHERE device_id=%s AND temp_x10 IS NOT NULL "
                        "AND humi_x10 IS NOT NULL AND lux IS NOT NULL "
                        "ORDER BY id DESC LIMIT 1", (AIR_DEVICE_ID,))
            row = cur.fetchone()
            t, h, lux = photo_air_snapshot(row)
    except Exception:
        pass
    finally:
        conn.close()
    rel = photo_save(dev, t, h, lux, jpeg)
    print("[截图] %s 截取存帧 -> %s" % (dev, rel))
    return rel


def photo_air_snapshot(row):
    """照片表不接受 NULL；没有完整空气快照时统一记录 -1 哨兵。"""
    if not row or any(value is None for value in row):
        return -1, -1, -1
    return row


def recorder_loop():
    """后台：每 SNAP_INTERVAL_SEC 截一帧；空帧/失败则 5s 后重试一次"""
    if SNAP_INTERVAL_SEC <= 0:
        return
    print("[OK] 定时截取：每 %d 秒从推流存一帧（空帧自动重试）" % SNAP_INTERVAL_SEC)
    while True:
        time.sleep(SNAP_INTERVAL_SEC)
        if save_snapshot() is None:
            time.sleep(5)
            save_snapshot()

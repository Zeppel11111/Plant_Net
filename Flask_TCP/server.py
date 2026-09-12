# -*- coding: utf-8 -*-
"""
============================================================
植物生长环境监测 —— 数据服务器（Flask + MySQL）
============================================================
这个文件做三件事（按顺序看）：

  1. TCP 服务（端口 9000）：接收设备（STM32）上传的数据
     设备连上来后一行一行发，每行格式：
         设备ID,温度x10,湿度x10,光照\n      例：dev01,268,391,440
     也兼容不带设备ID的老格式：268,391,440   → 自动记为 dev01
     （x10 表示放大十倍：268 就是 26.8 ℃，湿度同理，光照是整数）

  2. 存数据库：每收到一行，写进 MySQL 的 plant_db.sensor_data 表
     （库和表第一次运行自动创建，不用手动建）

  3. 网页（端口 8080）：浏览器打开，显示最新数据 + 最近记录，
     页面每 5 秒自动刷新

运行步骤见 README.md。
============================================================
"""
import os
import socket
import threading
import html
import json
import sys
import time
from datetime import datetime

import pymysql
from flask import Flask, jsonify, request, send_from_directory, Response

# ============================================================
# 一、配置区 —— 本地 MySQL 凭据不进公开仓库
# ============================================================
try:
    # local_config.py 由 local_config.example.py 复制而来，已被 Git 忽略。
    from local_config import MYSQL_HOST, MYSQL_USER, MYSQL_PASSWORD, MYSQL_DB
except ImportError:
    MYSQL_HOST = os.getenv("PLANT_NET_MYSQL_HOST", "127.0.0.1")
    MYSQL_USER = os.getenv("PLANT_NET_MYSQL_USER", "root")
    MYSQL_PASSWORD = os.getenv("PLANT_NET_MYSQL_PASSWORD", "")
    MYSQL_DB = os.getenv("PLANT_NET_MYSQL_DB", "plant_db")

TCP_PORT = 9000     # 设备上传数据的端口（STM32 连这里）
WEB_PORT = 8080     # 网页端口（浏览器访问这里）

SNAP_INTERVAL_SEC = 60   # 从 CAM 推流中定时截取存库（秒）；0 = 关闭自动截取
RECORD_DEVICE = "cam1"     # 自动截帧/上报所用设备 ID

# ============================================================
# 二、数据库小工具：连库 / 建库建表 / 存一行
# ============================================================
def get_conn():
    """连上 MySQL 返回连接对象。每次调用都是新连接，用完要关（防止多线程抢用）。"""
    return pymysql.connect(host=MYSQL_HOST, user=MYSQL_USER,
                           password=MYSQL_PASSWORD,
                           charset="utf8mb4", database=MYSQL_DB)


def init_db():
    """第一次启动自动建库建表。表已存在时不会报错（IF NOT EXISTS）。"""
    if not MYSQL_PASSWORD or MYSQL_PASSWORD == "YOUR_MYSQL_PASSWORD":
        raise RuntimeError(
            "未配置 MySQL 密码：复制 local_config.example.py 为 local_config.py 并填写密码，"
            "或设置环境变量 PLANT_NET_MYSQL_PASSWORD"
        )
    # 第一步：先连一个"没指定库"的连接，用来创建数据库本身
    conn = pymysql.connect(host=MYSQL_HOST, user=MYSQL_USER,
                           password=MYSQL_PASSWORD, charset="utf8mb4")
    try:
        with conn.cursor() as cur:
            cur.execute("CREATE DATABASE IF NOT EXISTS %s "
                        "DEFAULT CHARACTER SET utf8mb4" % MYSQL_DB)
        conn.commit()
    finally:
        conn.close()

    # 第二步：连到目标库，创建数据表
    conn = get_conn()
    try:
        with conn.cursor() as cur:
            cur.execute("""
                CREATE TABLE IF NOT EXISTS sensor_data (
                    id        INT AUTO_INCREMENT PRIMARY KEY,
                    device_id VARCHAR(20) NOT NULL DEFAULT 'dev01',
                    temp_x10  SMALLINT    NOT NULL COMMENT '温度*10, 268=26.8C',
                    humi_x10  SMALLINT    NOT NULL COMMENT '湿度*10',
                    lux       INT         NOT NULL COMMENT '光照',
                    ts        DATETIME    NOT NULL DEFAULT CURRENT_TIMESTAMP,
                    INDEX idx_ts (ts)
                ) DEFAULT CHARSET=utf8mb4
            """)
        conn.commit()
    finally:
        conn.close()

    # 第三步：照片记录表（ESP-CAM 网关：图片落盘 photos/，库中只存路径+读数快照）
    conn = get_conn()
    try:
        with conn.cursor() as cur:
            cur.execute("""
                CREATE TABLE IF NOT EXISTS photo_records (
                    id         INT AUTO_INCREMENT PRIMARY KEY,
                    device_id  VARCHAR(20) NOT NULL DEFAULT 'cam1',
                    image_path VARCHAR(255) NOT NULL COMMENT '相对路径 photos/xxx.jpg',
                    temp_x10   SMALLINT    NOT NULL DEFAULT -1 COMMENT '拍照时读数快照',
                    humi_x10   SMALLINT    NOT NULL DEFAULT -1,
                    lux        INT         NOT NULL DEFAULT -1,
                    ts         DATETIME    NOT NULL DEFAULT CURRENT_TIMESTAMP,
                    INDEX idx_ts (ts)
                ) DEFAULT CHARSET=utf8mb4
            """)
        conn.commit()
    finally:
        conn.close()
    print("[OK] 数据库已就绪:", MYSQL_DB)


def save_row(device_id, temp_x10, humi_x10, lux):
    """把一行数据插进数据库。出错只打印原因，绝不让服务器崩溃。"""
    conn = get_conn()
    try:
        with conn.cursor() as cur:
            cur.execute(
                "INSERT INTO sensor_data (device_id, temp_x10, humi_x10, lux) "
                "VALUES (%s, %s, %s, %s)",
                (device_id, temp_x10, humi_x10, lux))
        conn.commit()
    except Exception as e:
        print("[错误] 存库失败:", e)
    finally:
        conn.close()

# ============================================================
# 三、解析设备发来的每一行文本
# ============================================================
def parse_line(line):
    """解析一行，成功返回 (设备ID, 温度x10, 湿度x10, 光照)，失败返回 None。
    例："dev01,268,391,440" → ('dev01', 268, 391, 440)
        "268,391,440"       → ('dev01', 268, 391, 440)  （老格式补默认ID）
    """
    parts = line.split(",")
    if len(parts) == 3:                 # 老格式：没有设备ID
        device_id = "dev01"
        nums = parts
    elif len(parts) == 4:               # 新格式：第一个字段是设备ID
        device_id = parts[0].strip()
        nums = parts[1:]
    else:
        return None                     # 字段数不对，不是合法数据行

    try:
        temp_x10 = int(nums[0])
        humi_x10 = int(nums[1])
        lux      = int(nums[2])
    except ValueError:
        return None                     # 有非数字内容，不是合法数据行
    return (device_id, temp_x10, humi_x10, lux)

# ============================================================
# 四、TCP 服务器：等设备来连，收数据
# ============================================================
def handle_client(conn, addr):
    """处理一个设备的连接：持续收数据，按换行符切成一行行处理，直到设备断开。
    兜底：有些发送端不给每行结尾加 \\n（如旧固件），此时 0.2 秒没有新数据
    就认为"这一批发完了"，把缓冲区里剩余内容也当一行处理。"""
    print("[连接] 设备接入:", addr)
    buf = b""
    conn.settimeout(0.2)                    # 0.2 秒没新数据 = 触发空闲兜底
    try:
        while True:
            try:
                chunk = conn.recv(1024)
                if not chunk:               # 收到空数据 = 设备断开
                    break
                buf += chunk
            except socket.timeout:
                # 空闲兜底：缓冲里还有内容且没等到换行 → 直接当一行处理
                if buf:
                    handle_line(buf.strip())
                    buf = b""
                continue
            # 正常路径：按 \\n 把完整的一行行切出来
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                if line.strip():
                    handle_line(line.strip())
    except Exception as e:
        print("[连接异常]", addr, e)
    finally:
        if buf:                             # 设备断开前没发完的也处理掉
            handle_line(buf.strip())
        conn.close()
        print("[断开] 设备离开:", addr)


def handle_line(line):
    """收到一行文本 → 解析 → 入库 → 打印到控制台方便观察。"""
    try:
        text = line.decode("utf-8")
    except UnicodeDecodeError:
        return
    row = parse_line(text)
    if row is None:
        print("[警告] 无法解析的行:", text)
        return
    device_id, temp_x10, humi_x10, lux = row
    save_row(device_id, temp_x10, humi_x10, lux)
    # 控制台显示换算成带小数的人读格式（负温度用 int() 截断再取余）
    t_show = "%d.%d" % (int(temp_x10 / 10), abs(temp_x10) % 10)
    h_show = "%d.%d" % (int(humi_x10 / 10), humi_x10 % 10)
    print("[收到] %s  温度=%sC  湿度=%s%%  光照=%d"
          % (device_id, t_show, h_show, lux))


def tcp_server():
    """在后台线程里跑：一直等设备来连接，来一个开一个处理线程。"""
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", TCP_PORT))     # 0.0.0.0 = 允许所有网卡连入
    srv.listen(5)
    print("[OK] TCP 接收服务已启动，端口:", TCP_PORT)
    while True:
        conn, addr = srv.accept()       # 阻塞等待设备连接
        threading.Thread(target=handle_client, args=(conn, addr),
                         daemon=True).start()

# ============================================================
# 五、网页：显示最新数据和最近记录
# ============================================================
app = Flask(__name__)


def fetch_recent(limit):
    """从数据库取最近 limit 条，新的在前。返回元组列表。"""
    conn = get_conn()
    try:
        with conn.cursor() as cur:
            cur.execute("SELECT device_id, temp_x10, humi_x10, lux, ts "
                        "FROM sensor_data ORDER BY id DESC LIMIT %s", (limit,))
            return cur.fetchall()
    finally:
        conn.close()


def tfmt(x10):
    """温度显示：268 → "26.8"（负数也正确处理）"""
    return "%d.%d" % (int(x10 / 10), abs(x10) % 10)


def hfmt(x10):
    """湿度显示：391 → "39.1" """
    return "%d.%d" % (int(x10 / 10), x10 % 10)


@app.route("/")
def index():
    # 取最近 300 条（足够画一条能看的曲线），倒序（新的在前）
    rows = fetch_recent(300)

    # 网页表格：新的在前，直接显示最近 20 条
    rows_html = ""
    for r in rows[:20]:
        rows_html += ("<tr><td>%s</td><td>%s</td><td>%s</td><td>%d</td><td>%s</td></tr>"
                      % (html.escape(r[0]), tfmt(r[1]), hfmt(r[2]), r[3],
                         r[4].strftime("%m-%d %H:%M:%S")))

    # 曲线数据：升序（旧的在前），每点 = [秒级时间戳, 温度x10, 湿度x10, 光照]
    # 前端 JS 用这个数组画三条曲线 + 更新大字读数
    points = [[int(r[4].timestamp()), r[1], r[2], r[3]]
              for r in reversed(rows)]
    points_json = json.dumps(points)

    page = (PAGE_HTML
            .replace("@@ROWS@@", rows_html)
            .replace("@@POINTS@@", points_json))
    return page


@app.route("/api/latest")
def latest():
    # 前端每 5 秒拉一次这个接口，原地更新曲线/表格，不做整页刷新
    rows = fetch_recent(300)
    points = [[int(r[4].timestamp()), r[1], r[2], r[3]]
              for r in reversed(rows)]
    data_rows = [{"device": r[0], "temp": tfmt(r[1]), "humi": hfmt(r[2]),
                  "lux": r[3], "time": r[4].strftime("%m-%d %H:%M:%S")}
                 for r in rows[:20]]
    return jsonify(points=points, rows=data_rows)


# ============================================================
# 五点五、HTTP 上报端点（ESP-CAM 网关直连；TCP 9000 通道保留作双轨对比）
#   数据：POST /api/data    body JSON {"t":254,"h":601,"lux":123,"dev":"cam1"}
#   照片：POST /api/photo   body 为 JPEG 原始字节；query 带 t/h/lux/dev
#         -> 图片落盘 Flask_TCP/photos/，路径写 photo_records
#   看图：GET  /photos/xxx.jpg  （验证照片是否正常）
# ============================================================
PHOTO_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "photos")


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


@app.route("/api/data", methods=["POST"])
def api_data():
    """ESP-CAM 数据链：JSON {t,h,lux,dev} → sensor_data 一行。
    t/h/lux 为 x10 整数（254=25.4℃），与 TCP 通道同一套库同一套约定。
    同时记录设备来源 IP —— 之后 /api/snap 靠它找到 CAM 去拉图。"""
    j = request.get_json(force=True, silent=True) or {}
    t, h, lux = j.get("t"), j.get("h"), j.get("lux")
    if t is None or h is None or lux is None:
        return jsonify(ok=False, err="missing t/h/lux"), 400
    dev = j.get("dev", "cam1")
    try:
        CAM_IP[dev] = request.remote_addr          # 学到 CAM 在局域网里的 IP
        save_row(dev, int(t), int(h), int(lux))
    except Exception as e:
        print("[错误] /api/data 失败:", e)
        return jsonify(ok=False, err=str(e)), 500
    return jsonify(ok=True)


@app.route("/api/photo", methods=["POST"])
def api_photo():
    """ESP-CAM 图像链：body=JPEG 原始字节；query: t/h/lux/dev → 落盘+photo_records"""
    jpeg = request.get_data()
    if not jpeg:
        return jsonify(ok=False, err="empty body"), 400
    dev = request.args.get("dev", "cam1")
    try:
        t = int(request.args.get("t", "-1"))
        h = int(request.args.get("h", "-1"))
        lux = int(request.args.get("lux", "-1"))
    except ValueError:
        return jsonify(ok=False, err="bad query"), 400
    try:
        rel = photo_save(dev, t, h, lux, jpeg)
    except Exception as e:
        print("[错误] /api/photo 失败:", e)
        return jsonify(ok=False, err=str(e)), 500
    print("[照片] %s  保存 %s  (t=%d h=%d lux=%d)" % (dev, rel, t, h, lux))
    return jsonify(ok=True, path=rel)


@app.route("/photos/<path:name>")
def photo_file(name):
    """验证用：浏览器/curl 直接看落盘照片 http://<ip>:8080/photos/xxx.jpg"""
    return send_from_directory(PHOTO_DIR, name)


# ============================================================
# 五点六、监控截帧：CAM 常开视频流（本质 CCTV）；本服务按 SNAP_INTERVAL_SEC
#   定时（或手动按钮）拉 CAM 单帧 GET http://<camIP>:81/snap → 落盘入库。
#   CAM IP 从最近一条 /api/data 的来源 IP 自动学到（CAM_IP 字典），无需配置。
#   网页实时监控画面 = 浏览器直接指向 http://<camIP>:82/stream（MJPEG）。
# ============================================================
SNAP_PORT = 80        # CAM 相机服务端口（例程自带 httpd）
SNAP_PATH = "/capture"   # 取单帧路径（例程自带）；监控流同端口 /stream
STREAM_PORT = 80
CAM_IP = {}


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
                        "WHERE device_id=%s ORDER BY id DESC LIMIT 1", (dev,))
            row = cur.fetchone()
            if row:
                t, h, lux = row
    except Exception:
        pass
    finally:
        conn.close()

    rel = photo_save(dev, t, h, lux, jpeg)
    print("[截图] %s -> %s  (读数 t=%d h=%d lux=%d)" % (dev, rel, t, h, lux))
    return rel, None


@app.route("/api/snap")
def api_snap():
    """手动截取：GET /api/snap → 从最新推流帧存一张（空帧返回 503）"""
    rel = save_snapshot(request.args.get("dev", RECORD_DEVICE))
    if not rel:
        return jsonify(ok=False, err="暂无可存帧（推流未连接/空帧）"), 503
    return jsonify(ok=True, path=rel)


@app.route("/api/frame.jpg")
def api_frame():
    """单帧：网页/调试用（实时画面建议用 /api/live）"""
    jpeg, ts = get_latest_frame()
    if not jpeg:
        return "", 404, {"Content-Type": "image/jpeg"}
    return jpeg, 200, {"Content-Type": "image/jpeg", "Cache-Control": "no-store"}


@app.route("/api/live")
def api_live():
    """MJPEG 实时流：把内存里最新推流帧连续发给浏览器（帧率≈CAM 推流节奏）"""
    def gen():
        last = None
        while True:
            jpeg, _ = get_latest_frame()
            if jpeg is not None and jpeg is not last:
                last = jpeg
                yield (b"--frame\r\nContent-Type: image/jpeg\r\n\r\n" + jpeg + b"\r\n")
            else:
                time.sleep(0.05)
    return Response(gen(), mimetype="multipart/x-mixed-replace; boundary=frame")


@app.route("/api/camip")
def api_camip():
    """网页用它拿 CAM 的 IP，把 <img> 指向 :82/stream 显示实时监控"""
    return jsonify(dev=RECORD_DEVICE, ip=CAM_IP.get(RECORD_DEVICE))


@app.route("/api/heartbeat", methods=["POST"])
def api_heartbeat():
    """CAM 在线心跳/IP 注册（与 STM32 传感器数据无关）。
    CAM 上电即每 HEARTBEAT_SEC 报一次，Flask 从来源 IP 学到它在局域网里的地址，
    之后监控画面(:82)与截帧(:81/snap)才能工作。"""
    j = request.get_json(force=True, silent=True) or {}
    dev = j.get("dev", RECORD_DEVICE)
    CAM_IP[dev] = request.remote_addr
    return jsonify(ok=True)


@app.route("/api/latestphoto")
def api_latestphoto():
    """网页监控：返回最近一张入库照片的路径/时间（照片由 CAM 按 PHOTO_PUSH_SEC 定时推图）"""
    conn = get_conn()
    try:
        with conn.cursor() as cur:
            cur.execute("SELECT image_path, ts, device_id FROM photo_records "
                        "ORDER BY id DESC LIMIT 1")
            row = cur.fetchone()
    except Exception:
        row = None
    finally:
        conn.close()
    if not row:
        return jsonify(path=None)
    return jsonify(path=row[0], time=row[1].strftime("%H:%M:%S"), dev=row[2])


# ============================================================
# 五点七、收流 + 定时截取（CAM 纯客户端推帧；Flask 校验空帧→重试/报错）
#   CAM 推流格式：每帧 "L:<len>\n" + JPEG 字节；Flask 校验 FFD8，保留最新有效帧，
#   定时截取存盘入库（photo_records）。网页实时画面 = 内存帧 /api/frame.jpg。
# ============================================================
PUSH_PORT = 9001
FRAME_LOCK = threading.Lock()
FRAME_BUF = {"jpeg": None, "ts": 0.0, "ok": 0, "bad": 0}


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
    # 附带最近一条读数快照（数据由 ESP8266 通道的 dev01 提供，取全局最新）
    t = h = lux = -1
    conn = get_conn()
    try:
        with conn.cursor() as cur:
            cur.execute("SELECT temp_x10, humi_x10, lux FROM sensor_data "
                        "ORDER BY id DESC LIMIT 1")
            row = cur.fetchone()
            if row:
                t, h, lux = row
    except Exception:
        pass
    finally:
        conn.close()
    rel = photo_save(dev, t, h, lux, jpeg)
    print("[截图] %s 截取存帧 -> %s" % (dev, rel))
    return rel


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


# ============================================================
# 五点八、照片墙：网页回看历史截图（缩略图网格 + 点开原图 + 当时读数）
#   数据源：photo_records；缩略图即原图（VGA 几十 KB），浏览器自动缩放
# ============================================================
GALLERY_LIMIT = 500


@app.route("/gallery")
def gallery():
    conn = get_conn()
    try:
        with conn.cursor() as cur:
            cur.execute("SELECT image_path, temp_x10, humi_x10, lux, ts "
                        "FROM photo_records ORDER BY id DESC LIMIT %s",
                        (GALLERY_LIMIT,))
            rows = cur.fetchall()
    finally:
        conn.close()

    cards = []
    for path, t, h, lux, ts in rows:
        url = "/" + path
        if t == -1 and h == -1 and lux == -1:      # 无读数快照（早期数据）
            readings = "无读数快照"
        else:
            readings = "%s℃ / %s%% / %s lux" % (tfmt(t), hfmt(h), lux)
        cards.append(
            '<figure class="card">'
            '<a href="{u}" target="_blank"><img src="{u}" loading="lazy" alt="{p}"></a>'
            '<figcaption><b>{ts}</b><span>{r}</span></figcaption>'
            '</figure>'.format(u=url, p=html.escape(path),
                               ts=ts.strftime("%m-%d %H:%M:%S"), r=readings))

    return (GALLERY_HTML
            .replace("@@CARDS@@", "\n".join(cards))
            .replace("@@COUNT@@", str(len(rows))))


GALLERY_HTML = """<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Plant_Net 照片墙</title>
<style>
:root{--ink:#15384e;--dim:rgba(21,56,78,.62);--glass:rgba(255,255,255,.42);
  --line:rgba(255,255,255,.72);--sans:"PingFang SC","Microsoft YaHei",system-ui,sans-serif;
  --mono:"Cascadia Code",Consolas,monospace}
*{box-sizing:border-box;margin:0;padding:0}
body{min-height:100vh;padding:clamp(20px,4vw,44px);color:var(--ink);font-family:var(--sans);
  background:radial-gradient(1200px 700px at 82% -10%,rgba(255,255,255,.95),transparent 60%),
             radial-gradient(900px 640px at -10% 12%,rgba(141,210,255,.70),transparent 58%),
             linear-gradient(165deg,#eaf7ff 0%,#c9e7ff 46%,#d9f1ff 100%)}
header{display:flex;justify-content:space-between;align-items:baseline;gap:16px;
  flex-wrap:wrap;margin-bottom:22px}
h1{font-size:clamp(20px,3vw,28px);font-weight:600;letter-spacing:.04em}
.meta{font-family:var(--mono);font-size:12px;color:var(--dim);margin-top:6px}
a.back{font-family:var(--mono);font-size:12px;color:var(--ink);text-decoration:none;
  padding:8px 14px;border-radius:10px;border:1px solid var(--line);background:var(--glass)}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(210px,1fr));gap:16px}
.card{overflow:hidden;border-radius:16px;border:1px solid var(--line);background:var(--glass);
  box-shadow:0 18px 40px -28px rgba(35,110,170,.35)}
.card img{display:block;width:100%;aspect-ratio:4/3;object-fit:cover;background:#0d2330}
.card figcaption{display:flex;flex-direction:column;gap:2px;padding:9px 12px 11px}
.card b{font-family:var(--mono);font-size:12px;font-weight:600}
.card span{font-family:var(--mono);font-size:11px;color:var(--dim)}
</style>
</head>
<body>
<header>
  <div>
    <h1>&#127807; 照片墙</h1>
    <div class="meta">共 @@COUNT@@ 张（最新在前，最多 500 张）· 点击图片看原图</div>
  </div>
  <a class="back" href="/">&larr; 返回监控</a>
</header>
<div class="grid">
@@CARDS@@
</div>
</body>
</html>
"""


# 页面模板：占位符 @@ROWS@@（表格行）、@@POINTS@@（曲线数据 JSON）
# 曲线用原生 canvas 手绘，不依赖任何外部库（离线也能看）
PAGE_HTML = """<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="color-scheme" content="light">
<title>植物生长观测站</title>
<style>
:root{
  --bg:#cfe9ff;
  --ink:#15384e;
  --ink-dim:rgba(21,56,78,.62);
  --ink-faint:rgba(21,56,78,.42);
  --line:rgba(255,255,255,.72);
  --line-soft:rgba(21,56,78,.08);
  --glass:rgba(255,255,255,.28);
  --glass-hi:rgba(255,255,255,.72);
  --terra:#ef7c5f;
  --green:#17b184;
  --amber:#e0a63a;
  --sans:"PingFang SC","HarmonyOS Sans SC","Microsoft YaHei UI","Microsoft YaHei",system-ui,sans-serif;
  --mono:"Cascadia Code","SF Mono",Consolas,"Courier New",monospace;
}
*{box-sizing:border-box;margin:0;padding:0}
html{background:var(--bg)}
body{
  position:relative;
  min-height:100vh;
  padding:clamp(28px,5vw,58px) clamp(16px,4vw,60px) 88px;
  color:var(--ink);
  font-family:var(--sans);
  overflow-x:hidden;
  -webkit-font-smoothing:antialiased;
  background:
    radial-gradient(1200px 700px at 82% -10%, rgba(255,255,255,.95), transparent 60%),
    radial-gradient(900px 640px at -10% 12%, rgba(141,210,255,.70), transparent 58%),
    radial-gradient(1000px 680px at 48% 116%, rgba(255,222,165,.55), transparent 60%),
    linear-gradient(165deg,#eaf7ff 0%,#c9e7ff 46%,#d9f1ff 100%);
}
body::before,body::after{
  content:"";position:fixed;z-index:0;border-radius:50%;
  filter:blur(70px);pointer-events:none;
}
body::before{
  width:52vw;height:52vw;left:-16vw;top:-14vh;
  background:radial-gradient(circle,rgba(255,255,255,.85),rgba(255,255,255,0) 68%);
  animation:driftA 26s ease-in-out infinite alternate;
}
body::after{
  width:44vw;height:44vw;right:-15vw;bottom:-12vh;
  background:radial-gradient(circle,rgba(255,204,124,.38),rgba(255,204,124,0) 66%);
  animation:driftB 32s ease-in-out infinite alternate;
}
.wrap{position:relative;z-index:1;max-width:1120px;margin:0 auto}
.num{font-family:var(--mono)}
::selection{background:rgba(23,177,132,.22)}

/* ---------- header ---------- */
header{display:flex;justify-content:space-between;align-items:flex-end;gap:24px;
       flex-wrap:wrap;margin-bottom:36px;animation:rise .6s cubic-bezier(.2,.7,.3,1) both}
.overline{display:flex;align-items:center;gap:10px;
  font-family:var(--mono);font-size:11px;letter-spacing:.34em;
  color:var(--ink-faint);text-transform:uppercase}
.overline::before{content:"";width:28px;height:1px;background:var(--green);
  box-shadow:0 0 9px rgba(23,177,132,.45)}
h1{display:flex;align-items:center;gap:14px;
  margin-top:14px;font-size:clamp(24px,3.6vw,35px);
  font-weight:600;letter-spacing:.04em;line-height:1.1}
h1 .leaf{display:grid;place-items:center;width:42px;height:42px;border-radius:13px;
  font-size:20px;line-height:1;
  background:linear-gradient(145deg,var(--glass-hi),var(--glass));
  border:1px solid var(--line);
  box-shadow:inset 0 1px 0 rgba(255,255,255,.85),0 14px 30px -18px rgba(35,110,170,.35);
  backdrop-filter:blur(14px) saturate(150%);
  -webkit-backdrop-filter:blur(14px) saturate(150%)}
.status{display:flex;align-items:center;gap:14px;padding:12px 18px;border-radius:16px;
  background:linear-gradient(145deg,var(--glass-hi),rgba(255,255,255,.40));
  border:1px solid var(--line);
  backdrop-filter:blur(18px) saturate(150%);
  -webkit-backdrop-filter:blur(18px) saturate(150%);
  box-shadow:0 18px 40px -28px rgba(35,110,170,.30),inset 0 1px 0 rgba(255,255,255,.9);
  animation:rise .6s cubic-bezier(.2,.7,.3,1) .08s both}
.live{display:inline-flex;align-items:center;gap:9px;
  font-family:var(--mono);font-size:12px;letter-spacing:.08em;color:var(--ink-dim)}
.dot{width:8px;height:8px;border-radius:50%;background:var(--green);
  box-shadow:0 0 0 0 rgba(23,177,132,.45);animation:pulse 2.2s infinite}
.dot.off{background:var(--ink-faint);animation:none}
@keyframes pulse{0%{box-shadow:0 0 0 0 rgba(23,177,132,.45)}
  70%{box-shadow:0 0 0 11px rgba(23,177,132,0)}
  100%{box-shadow:0 0 0 0 rgba(23,177,132,0)}}
.stime{font-family:var(--mono);font-size:12px;color:var(--ink-faint);
  letter-spacing:.06em;padding-left:14px;border-left:1px solid var(--line-soft)}

/* ---------- reading cards ---------- */
.cards{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:18px;margin-bottom:20px}
.card,.chart-card,.table-card{position:relative;overflow:hidden;border-radius:22px;
  background:linear-gradient(150deg,rgba(255,255,255,.76),rgba(255,255,255,.30) 55%,rgba(255,255,255,.46));
  border:1px solid var(--line);
  backdrop-filter:blur(24px) saturate(160%);
  -webkit-backdrop-filter:blur(24px) saturate(160%);
  box-shadow:0 22px 52px -30px rgba(35,110,170,.30),inset 0 1px 0 rgba(255,255,255,.9)}
.card{padding:24px 22px 19px;animation:rise .55s cubic-bezier(.2,.7,.3,1) both}
.card:nth-child(1){animation-delay:.06s}
.card:nth-child(2){animation-delay:.14s}
.card:nth-child(3){animation-delay:.22s}
.card::before{content:"";position:absolute;left:0;top:22%;bottom:22%;width:2px;border-radius:2px;
  background:linear-gradient(transparent,var(--c),transparent);opacity:.95}
.card::after{content:"";position:absolute;right:-64px;top:-74px;width:230px;height:230px;
  border-radius:50%;pointer-events:none;
  background:radial-gradient(circle,color-mix(in srgb,var(--c) 15%,transparent),transparent 66%)}
.kind{display:flex;align-items:center;gap:9px;
  font-family:var(--mono);font-size:10px;letter-spacing:.24em;
  color:var(--ink-dim);text-transform:uppercase}
.kind::before{content:"";width:6px;height:6px;border-radius:50%;flex:none;
  background:var(--c);box-shadow:0 0 10px var(--c)}
.big{display:flex;align-items:baseline;gap:6px;margin:16px 0 9px;
  font-family:var(--mono);font-weight:500;line-height:1;
  font-size:clamp(34px,3.4vw,42px);font-variant-numeric:tabular-nums}
.big .u{font-family:var(--sans);font-size:13px;font-weight:500;
  letter-spacing:.08em;color:var(--ink-faint)}
.note{font-family:var(--mono);font-size:10.5px;letter-spacing:.03em;color:var(--ink-faint)}

/* ---------- chart card ---------- */
.chart-card{padding:22px 24px 14px;margin-bottom:20px;
  animation:rise .6s cubic-bezier(.2,.7,.3,1) .26s both}
.chart-head{display:flex;justify-content:space-between;align-items:baseline;gap:12px;margin-bottom:2px}
.chart-head h2{display:flex;align-items:center;gap:9px;
  font-size:15px;font-weight:600;letter-spacing:.09em}
.chart-head h2::before{content:"";width:5px;height:15px;border-radius:3px;
  background:linear-gradient(180deg,var(--green),rgba(23,177,132,.08))}
.chart-head .hint{font-family:var(--mono);font-size:10.5px;
  color:var(--ink-faint);letter-spacing:.04em;text-align:right}
.series{margin:12px 0 3px}
.series canvas{width:100%;height:118px;display:block}
.legend{display:flex;gap:22px;margin:8px 0 2px;flex-wrap:wrap;
  font-family:var(--mono);font-size:11px;letter-spacing:.05em;color:var(--ink-dim)}
.legend i{display:inline-block;width:17px;height:2px;border-radius:2px;
  vertical-align:middle;margin-right:8px}

/* ---------- table card ---------- */
.table-card{padding:22px 24px 16px;animation:rise .65s cubic-bezier(.2,.7,.3,1) .34s both}
table{width:100%;border-collapse:collapse;font-size:13px}
.scroll{max-height:300px;overflow-y:auto;margin-top:8px;scrollbar-width:thin;
  scrollbar-color:rgba(21,56,78,.22) transparent}
.scroll::-webkit-scrollbar{width:8px}
.scroll::-webkit-scrollbar-thumb{background:rgba(21,56,78,.16);border-radius:8px}
.scroll::-webkit-scrollbar-track{background:transparent}
thead th{position:sticky;top:0;z-index:1;
  background:linear-gradient(180deg,rgba(255,255,255,.92),rgba(255,255,255,.70));
  backdrop-filter:blur(10px);
  -webkit-backdrop-filter:blur(10px)}
th{font-family:var(--mono);font-size:10px;font-weight:600;letter-spacing:.17em;
  color:var(--ink-faint);text-align:left;padding:12px 10px;
  border-bottom:1px solid var(--line-soft);white-space:nowrap}
td{padding:11px 10px;border-bottom:1px solid var(--line-soft);
  color:var(--ink-dim);white-space:nowrap;transition:background .18s ease}
tbody td:nth-child(2),tbody td:nth-child(3),tbody td:nth-child(4){font-family:var(--mono);color:var(--ink)}
tbody td:last-child{font-family:var(--mono);font-size:12px;color:var(--ink-faint)}
tbody td:first-child{font-family:var(--mono);font-weight:500;color:var(--ink);letter-spacing:.06em}
tbody tr:nth-child(even){background:rgba(255,255,255,.32)}
tbody tr:hover{background:rgba(44,140,190,.10)}
tbody tr:last-child td{border-bottom:none}

/* ---------- motion ---------- */
@keyframes rise{from{opacity:0;transform:translateY(14px) scale(.995)}
  to{opacity:1;transform:none}}
@keyframes driftA{from{transform:translate3d(0,0,0) scale(1)}
  to{transform:translate3d(7vw,5vh,0) scale(1.16)}}
@keyframes driftB{from{transform:translate3d(0,0,0) scale(1.12)}
  to{transform:translate3d(-6vw,-4vh,0) scale(.95)}}
@media(prefers-reduced-motion:reduce){
  *,*::before,*::after{animation-duration:.01ms!important;animation-iteration-count:1!important;transition:none!important}
}

@media(max-width:860px){
  header{align-items:flex-start;flex-direction:column}
  .status{align-self:stretch;justify-content:space-between}
  .cards{grid-template-columns:1fr}
}
@media(max-width:520px){
  body{padding:22px 14px 64px}
  .chart-card,.table-card{padding:19px 17px 12px}
  .chart-head{flex-direction:column;align-items:flex-start;gap:6px}
  .chart-head .hint{text-align:left}
}
</style>
</head>
<body>
<div class="wrap">

  <header>
    <div>
      <div class="overline">Plant Environment // Field Log</div>
      <h1><span class="leaf">&#127807;</span> 植物生长观测站</h1>
    </div>
    <div class="status">
      <div><span class="live"><span class="dot" id="dot"></span><span id="liveText">数据流监测中</span></span></div>
      <div class="stime" id="stime">--:--:--</div>
      <button id="snapBtn" onclick="snapNow()" style="margin-left:6px;padding:8px 14px;border-radius:10px;border:1px solid var(--line);background:linear-gradient(145deg,var(--glass-hi),rgba(255,255,255,.4));font-family:var(--sans);font-size:13px;color:var(--ink);cursor:pointer">拍照存库</button>
      <a href="/gallery" target="_blank" style="margin-left:6px;padding:8px 14px;border-radius:10px;border:1px solid var(--line);background:linear-gradient(145deg,var(--glass-hi),rgba(255,255,255,.4));font-family:var(--sans);font-size:13px;color:var(--ink);text-decoration:none">照片墙</a>
    </div>
    <div id="snapBox" style="margin:10px 0 0;display:none"></div>
  </header>

  <section class="chart-card" id="monBox" style="display:none;margin-bottom:20px">
    <div class="chart-head">
      <h2><span style="width:5px;height:15px;border-radius:3px;background:linear-gradient(180deg,#17b184,rgba(23,177,132,.08));display:inline-block;margin-right:9px"></span>实时监控</h2>
      <div class="hint">MJPEG 流 · 页面不保存 · 截图走"拍照存库"入库</div>
    </div>
    <img id="liveCam" alt="监控画面" style="width:100%;border-radius:14px;display:block;background:#0d2330">
    <div class="hint" id="monHint" style="margin-top:6px">等待设备上报，获取 CAM 地址…</div>
  </section>

  <section class="cards">
    <div class="card" style="--c:var(--terra)">
      <div class="kind">TEMPERATURE · 温度</div>
      <div class="big num"><span id="vTemp">--</span><span class="u"> ℃</span></div>
      <div class="note" id="nTemp">--</div>
    </div>
    <div class="card" style="--c:var(--green)">
      <div class="kind">HUMIDITY · 湿度</div>
      <div class="big num"><span id="vHumi">--</span><span class="u"> %RH</span></div>
      <div class="note" id="nHumi">--</div>
    </div>
    <div class="card" style="--c:var(--amber)">
      <div class="kind">LIGHT · 光照</div>
      <div class="big num"><span id="vLux">--</span><span class="u"> LUX</span></div>
      <div class="note" id="nLux">--</div>
    </div>
  </section>

  <section class="chart-card">
    <div class="chart-head">
      <h2>最近采样曲线</h2>
      <div class="hint">最近 300 条 · 每 5 秒自动刷新</div>
    </div>
    <div class="legend">
      <span><i style="background:var(--terra)"></i>温度 ℃</span>
      <span><i style="background:var(--green)"></i>湿度 %RH</span>
      <span><i style="background:var(--amber)"></i>光照 LUX</span>
    </div>
    <div class="series"><canvas id="chTemp"></canvas></div>
    <div class="series"><canvas id="chHumi"></canvas></div>
    <div class="series"><canvas id="chLux"></canvas></div>
  </section>

  <section class="table-card">
    <div class="chart-head" style="margin-bottom:8px">
      <h2>最近记录</h2>
      <div class="hint">最新 20 条</div>
    </div>
    <div class="scroll">
      <table>
        <thead><tr><th>设备</th><th>温度 ℃</th><th>湿度 %RH</th><th>光照 LUX</th><th>时间</th></tr></thead>
        <tbody>
@@ROWS@@
        </tbody>
      </table>
    </div>
  </section>

</div>

<script>
/* 曲线数据：[秒级时间戳, 温度x10, 湿度x10, 光照]，按时间升序（旧的在前） */
var DATA = @@POINTS@@;

/* ---------- 工具 ---------- */
function fmtHM(ep){var d=new Date(ep*1000);function p(n){return (n<10?"0":"")+n}
  return p(d.getHours())+":"+p(d.getMinutes())+":"+p(d.getSeconds());}

/* 读数卡片与状态：用最新一点更新 */
var API=(location.protocol==="http:"||location.protocol==="https:")?"/api/latest":null;

function renderHead(){
  var dot=document.getElementById("dot"), lt=document.getElementById("liveText"),
      st=document.getElementById("stime");
  if(DATA.length===0){
    dot.className="dot off";
    lt.textContent="等待数据…";
    st.textContent="--:--:--";
    return;
  }
  var last=DATA[DATA.length-1];
  st.textContent=fmtHM(last[0]);
  document.getElementById("vTemp").textContent=(last[1]/10).toFixed(1);
  document.getElementById("vHumi").textContent=(last[2]/10).toFixed(1);
  document.getElementById("vLux").textContent=last[3];
  var d=new Date(last[0]*1000);
  var note="最近更新 "+d.getFullYear()+"-"+("0"+(d.getMonth()+1)).slice(-2)+"-"+("0"+d.getDate()).slice(-2)+" "+fmtHM(last[0]);
  document.getElementById("nTemp").textContent=note;
  document.getElementById("nHumi").textContent=note;
  document.getElementById("nLux").textContent=note;
}

/* 漂亮的刻度：把 [lo,hi] 分成大约 n 段的整数步长 */
function niceTicks(lo,hi,n){
  if(hi<=lo)return [lo];
  var rough=(hi-lo)/n, mag=Math.pow(10,Math.floor(Math.log(rough)/Math.LN10));
  var norm=rough/mag, step=norm<1.5?1:norm<3?2:norm<7?5:10; step*=mag;
  var out=[]; for(var v=Math.ceil(lo/step)*step; v<=hi+step*0.001; v+=step) out.push(v);
  return out;
}

/* 画一条曲线：values 为数值数组，epochs 为对应时间戳 */
function drawSeries(canvasId, values, epochs, color){
  var cv=document.getElementById(canvasId);
  var cssW=cv.parentElement.clientWidth, cssH=118;
  var dpr=window.devicePixelRatio||1;
  cv.width=cssW*dpr; cv.height=cssH*dpr;
  var ctx=cv.getContext("2d");
  ctx.setTransform(dpr,0,0,dpr,0,0);
  ctx.clearRect(0,0,cssW,cssH);
  if(values.length<2){
    ctx.fillStyle="rgba(21,56,78,.36)";ctx.font="12px 'Cascadia Code',Consolas,monospace";
    ctx.fillText("数据采集中，稍候…",12,cssH/2);
    return;
  }
  /* y 范围：留 12% 上下余量 */
  var lo=Math.min.apply(null,values), hi=Math.max.apply(null,values);
  var pad=(hi-lo)*0.15||1; lo-=pad; hi+=pad;
  var padL=46, padR=14, padT=10, padB=18;
  var pw=cssW-padL-padR, ph=cssH-padT-padB;
  var X=function(i){return padL+pw*i/(values.length-1);};
  var Y=function(v){return padT+ph*(1-(v-lo)/(hi-lo));};
  /* 网格与刻度 */
  ctx.strokeStyle="rgba(21,56,78,.08)";ctx.fillStyle="rgba(21,56,78,.38)";
  ctx.font="10px 'Cascadia Code',Consolas,monospace";ctx.lineWidth=1;
  var ticks=niceTicks(lo,hi,4);
  for(var k=0;k<ticks.length;k++){
    var y=Y(ticks[k]);
    ctx.beginPath();ctx.moveTo(padL,y);ctx.lineTo(cssW-padR,y);ctx.stroke();
    ctx.textAlign="right";ctx.fillText(ticks[k].toFixed(ticks[k]%1?1:0),padL-6,y+3);
  }
  /* 横轴时间刻度：5 个 */
  for(var t=0;t<5;t++){
    var i=Math.round(t*(values.length-1)/4);
    var x=X(i);
    ctx.beginPath();ctx.moveTo(x,cssH-padB);ctx.lineTo(x,cssH-padB+4);ctx.stroke();
    ctx.textAlign="center";ctx.fillText(fmtHM(epochs[i]),x,cssH-3);
  }
  /* 曲线 + 渐变面积 */
  ctx.lineWidth=1.6;ctx.strokeStyle=color;
  ctx.beginPath();
  for(var i2=0;i2<values.length;i2++){
    var x2=X(i2),y2=Y(values[i2]);
    if(i2===0)ctx.moveTo(x2,y2);else ctx.lineTo(x2,y2);
  }
  ctx.stroke();
  var g=ctx.createLinearGradient(0,padT,0,cssH-padB);
  g.addColorStop(0,color+"33");g.addColorStop(1,color+"00");
  ctx.lineTo(X(values.length-1),cssH-padB);ctx.lineTo(X(0),cssH-padB);
  ctx.closePath();ctx.fillStyle=g;ctx.fill();
  /* 末端圆点 + 数值 */
  var lx=X(values.length-1), ly=Y(values[values.length-1]);
  ctx.fillStyle=color;ctx.beginPath();ctx.arc(lx,ly,3.5,0,6.2832);ctx.fill();
  ctx.fillStyle=color;ctx.font="600 11px 'Cascadia Code',Consolas,monospace";
  ctx.textAlign="left";ctx.fillText(values[values.length-1],lx+8,ly-6);
}

/* 空状态 */
function renderEmpty(){
  document.querySelectorAll(".series canvas").forEach(function(cv){
    var ctx=cv.getContext("2d"), cssW=cv.parentElement.clientWidth;
    cv.width=cssW;cv.height=118;
    ctx.fillStyle="rgba(21,56,78,.36)";ctx.font="13px 'Cascadia Code',Consolas,monospace";
    ctx.fillText("还没有数据 —— 板子连接后自动出现",cssW/2-130,59);
  });
}

/* 入口 */
function chartArrays(){
  var eps=[],t=[],h=[],l=[];
  for(var i=0;i<DATA.length;i++){
    eps.push(DATA[i][0]);t.push(DATA[i][1]/10);
    h.push(DATA[i][2]/10);l.push(DATA[i][3]);
  }
  return {eps:eps,t:t,h:h,l:l};
}

function drawAll(){
  if(DATA.length===0){renderEmpty();renderHead();return;}
  var a=chartArrays();
  renderHead();
  drawSeries("chTemp",a.t,a.eps,"#ef7c5f");
  drawSeries("chHumi",a.h,a.eps,"#17b184");
  drawSeries("chLux",a.l,a.eps,"#e0a63a");
}

function esc(s){
  return String(s).replace(/[&<>"']/g,function(c){
    return {"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c];});
}

function renderRows(rows){
  var tb=document.querySelector("tbody");if(!tb)return;
  var html="";
  for(var i=0;i<rows.length;i++){
    var r=rows[i];
    html+="<tr><td>"+esc(r.device)+"</td><td>"+esc(r.temp)+"</td><td>"
        +esc(r.humi)+"</td><td>"+esc(r.lux)+"</td><td>"+esc(r.time)+"</td></tr>";
  }
  tb.innerHTML=html;
}

function setLive(ok){
  var dot=document.getElementById("dot"),lt=document.getElementById("liveText");
  var want=ok?"dot":"dot off",text=ok?"数据流监测中":"连接中断，重试中…";
  if(dot&&dot.className!==want)dot.className=want;
  if(lt&&lt.textContent!==text)lt.textContent=text;
}

function poll(){
  if(!API)return;
  fetch(API,{cache:"no-store"})
    .then(function(res){if(!res.ok)throw new Error("HTTP "+res.status);return res.json();})
    .then(function(d){
      if(d&&Array.isArray(d.points)){DATA=d.points;drawAll();}
      if(d&&Array.isArray(d.rows))renderRows(d.rows);
      setLive(true);
    })
    .catch(function(){setLive(false);});
}

/* 启动：先画当前数据，再每 5 秒静默更新 */
drawAll();
setInterval(poll,5000);

/* ---------- 拍照存库：服务器拉 CAM /snap → 落盘入库 → 页面上显示 ---------- */
function snapNow(){
  var b=document.getElementById("snapBtn"), box=document.getElementById("snapBox");
  if(!box)return;
  box.style.display="block";
  if(b){b.disabled=true;b.textContent="拍照中…";}
  box.innerHTML='<span style="font-family:var(--mono);font-size:12px;color:var(--ink-faint)">正在请 CAM 拍照…</span>';
  fetch("/api/snap",{cache:"no-store"})
    .then(function(r){return r.json();})
    .then(function(d){
      if(d.ok){
        box.innerHTML='<span style="font-family:var(--mono);font-size:12px;color:var(--ink)">已存库: '
          +d.path+'</span><br>'
          +'<a href="/'+d.path+'" target="_blank"><img src="/'+d.path
          +'" style="max-width:100%;max-height:360px;border-radius:12px;margin-top:6px;box-shadow:0 12px 30px -18px rgba(35,110,170,.5)"></a>';
      }else{
        box.innerHTML='<span style="font-family:var(--mono);font-size:12px;color:#c0392b">截图失败: '
          +(d.err||"未知")+'</span>';
      }
    })
    .catch(function(){box.innerHTML='<span style="font-family:var(--mono);font-size:12px;color:#c0392b">截图失败: 网络错误</span>';})
    .then(function(){if(b){b.disabled=false;b.textContent="拍照存库";}});
}

/* ---------- 监控：Flask MJPEG 实时流（/api/live，浏览器一次连接持续收帧） ---------- */
function refreshCam(){
  var box=document.getElementById("monBox"), img=document.getElementById("liveCam"),
      hint=document.getElementById("monHint");
  if(!box||!img)return;
  img.src="/api/live?v="+Date.now();     /* 首次挂流；带时间戳便于断流后重连 */
  box.style.display="block";
  if(hint)hint.textContent="实时画面（Flask 收 CAM 推流转播）· 每 60s 自动截取存库";
}
/* 每 20s 重挂一次流：浏览器断流自愈 */
setInterval(function(){
  var img=document.getElementById("liveCam");
  if(img) img.src="/api/live?v="+Date.now();
},20000);
refreshCam();
</script>
</body>
</html>
"""

# ============================================================
# 六、入口：顺序启动三件事
# ============================================================
if __name__ == "__main__":
    print("===== 植物环境监测数据服务器 =====")

    # 1. 数据库（MySQL 没启动或密码错会在这里报错并退出）
    try:
        init_db()
    except Exception as e:
        print("[错误] 连不上 MySQL:", e)
        print("       请确认：1) MySQL80 服务已启动  2) 配置区密码正确")
        input("按回车退出...")
        sys.exit(1)

    # 2. TCP 接收线程（收设备数据）
    threading.Thread(target=tcp_server, daemon=True).start()

    # 2.5 收流服务（CAM 纯客户端推帧）
    threading.Thread(target=push_listener, daemon=True).start()

    # 2.6 定时截取线程（SNAP_INTERVAL_SEC > 0 时启动）
    threading.Thread(target=recorder_loop, daemon=True).start()

    # 3. 网页（浏览器打开 http://127.0.0.1:8080）
    print("网页地址: http://127.0.0.1:%d" % WEB_PORT)
    print("（按 Ctrl+C 停止）")
    app.run(host="0.0.0.0", port=WEB_PORT, threaded=True)

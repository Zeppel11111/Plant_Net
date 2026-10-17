"""网页与 HTTP API：提供监控、历史照片和视频接口；业务处理委托给对应模块。"""

import html
import json
import time
from pathlib import Path
from flask import Flask, jsonify, request, send_from_directory, Response
from .config import PHOTO_DIR, RECORD_DEVICE
from .storage import get_conn, save_row, fetch_recent
from .dashboard import (tfmt, hfmt, dashboard_record, dashboard_series,
                        grouped_table_rows, api_row)
from .camera import (CAM_IP, photo_save, get_latest_frame, save_snapshot)


app = Flask(__name__)
TEMPLATE_DIR = Path(__file__).resolve().parent / "templates"
PAGE_HTML = (TEMPLATE_DIR / "index.html").read_text(encoding="utf-8")
GALLERY_HTML = (TEMPLATE_DIR / "gallery.html").read_text(encoding="utf-8")
GALLERY_LIMIT = 500

@app.route("/")
def index():
    # 取最近 300 条（足够画一条能看的曲线），倒序（新的在前）
    rows = fetch_recent(300)

    # 页面首屏与后续 API 使用同一套传感器分组规则。
    records = [dashboard_record(row) for row in rows]

    # 曲线数据：升序（旧的在前），每点 = [秒级时间戳, 温度x10, 湿度x10, 光照]
    # 前端 JS 用这个数组画三条曲线 + 更新大字读数
    points, soil_points, soil2_points = dashboard_series(records)
    points_json = json.dumps(points)
    soil_points_json = json.dumps(soil_points)
    soil2_points_json = json.dumps(soil2_points)
    table_rows_json = json.dumps(grouped_table_rows(records), ensure_ascii=False)

    page = (PAGE_HTML
            .replace("@@TABLE_ROWS@@", table_rows_json)
            .replace("@@POINTS@@", points_json)
            .replace("@@SOIL_POINTS@@", soil_points_json)
            .replace("@@SOIL2_POINTS@@", soil2_points_json))
    return page


@app.route("/api/latest")
def latest():
    # 前端每 5 秒拉一次这个接口，原地更新曲线/表格，不做整页刷新
    records = [dashboard_record(row) for row in fetch_recent(300)]
    points, soil_points, soil2_points = dashboard_series(records)
    return jsonify(points=points, soil_points=soil_points,
                   soil2_points=soil2_points,
                   table_rows=grouped_table_rows(records),
                   rows=[api_row(record) for record in records[:20]])


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

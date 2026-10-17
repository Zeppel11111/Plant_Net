import os
import shutil
import subprocess
import sys
import types
import unittest
from datetime import datetime
from pathlib import Path
from unittest.mock import patch


THIS_DIR = os.path.dirname(__file__)
SERVER_DIR = os.path.dirname(THIS_DIR)
if SERVER_DIR not in sys.path:
    sys.path.insert(0, SERVER_DIR)

if "pymysql" not in sys.modules:
    sys.modules["pymysql"] = types.SimpleNamespace(connect=lambda **kwargs: None)
if "flask" not in sys.modules:
    class _FakeResponse:
        def __init__(self, value):
            self.value = value
            self.status_code = 200

        def get_data(self, as_text=False):
            data = self.value if isinstance(self.value, str) else str(self.value)
            return data if as_text else data.encode("utf-8")

        def get_json(self):
            return self.value

    class _FakeClient:
        def __init__(self, app):
            self.app = app

        def get(self, path):
            return _FakeResponse(self.app.routes[path]())

    class _FakeFlask:
        def __init__(self, *args, **kwargs):
            self.routes = {}

        def route(self, *args, **kwargs):
            def register(function):
                self.routes[args[0]] = function
                return function
            return register

        def test_client(self):
            return _FakeClient(self)

    sys.modules["flask"] = types.SimpleNamespace(
        Flask=_FakeFlask,
        jsonify=lambda **kwargs: kwargs,
        request=types.SimpleNamespace(),
        send_from_directory=lambda *args, **kwargs: None,
        Response=object,
    )

from plant_server import camera, dashboard, storage, telemetry, web


class ParseLineTests(unittest.TestCase):
    def test_legacy_air_only_lines_use_default_device_and_air_mask(self):
        for line, device_id in (("268,391,440", "dev01"),
                                ("dev01,268,391,440", "dev01")):
            with self.subTest(line=line):
                row = telemetry.parse_line(line)
                self.assertEqual(row.device_id, device_id)
                self.assertEqual((row.temp_x10, row.humi_x10, row.lux),
                                 (268, 391, 440))
                self.assertEqual(row.valid_mask, 1)
                self.assertEqual(row.soil_temp_x10, None)
                self.assertEqual(row.soil_humi_x10, None)
                self.assertEqual(row.soil_ec, None)
                self.assertEqual(row.soil_salinity, None)
                self.assertEqual(row.soil_n, None)
                self.assertEqual(row.soil_p, None)
                self.assertEqual(row.soil_k, None)
                self.assertEqual(row.soil_ph_x10, None)

    def test_v2_line_exposes_all_named_measurements(self):
        row = telemetry.parse_line(
            "v2,dev01,3,268,612,1240,243,387,786,392,42,31,58,66")

        self.assertEqual(row.device_id, "dev01")
        self.assertEqual(row.valid_mask, 3)
        self.assertEqual(
            (row.temp_x10, row.humi_x10, row.lux,
             row.soil_temp_x10, row.soil_humi_x10, row.soil_ec,
             row.soil_salinity, row.soil_n, row.soil_p, row.soil_k,
             row.soil_ph_x10),
            (268, 612, 1240, 243, 387, 786, 392, 42, 31, 58, 66))

    def test_v2_mask_replaces_invalid_group_placeholders_with_none(self):
        air_only = telemetry.parse_line(
            "v2,dev01,1,268,612,1240,0,0,0,0,0,0,0,0")
        soil_only = telemetry.parse_line(
            "v2,dev01,2,0,0,0,243,387,786,392,42,31,58,66")

        self.assertEqual((air_only.temp_x10, air_only.humi_x10, air_only.lux),
                         (268, 612, 1240))
        self.assertEqual(air_only.soil_fields, (None,) * 8)
        self.assertEqual((soil_only.temp_x10, soil_only.humi_x10, soil_only.lux),
                         (None, None, None))
        self.assertEqual(soil_only.soil_fields,
                         (243, 387, 786, 392, 42, 31, 58, 66))

    def test_parse_line_rejects_malformed_records(self):
        bad_lines = (
            "v3,dev01,3,268,612,1240,243,387,786,392,42,31,58,66",
            "v2,,3,268,612,1240,243,387,786,392,42,31,58,66",
            "v2,dev01,3,268,612,1240",
            "v2,dev01,4,268,612,1240,243,387,786,392,42,31,58,66",
            "v2,dev01,nope,268,612,1240,243,387,786,392,42,31,58,66",
            "v2,dev 01,3,268,612,1240,243,387,786,392,42,31,58,66",
            "v2,device_id_is_too_long,3,268,612,1240,243,387,786,392,42,31,58,66",
            "dev 01,268,391,440",
            "dev01,268,nope,440",
        )
        for line in bad_lines:
            with self.subTest(line=line):
                self.assertIsNone(telemetry.parse_line(line))

    def test_parse_line_rejects_sql_integer_overflow(self):
        legacy_overflows = (
            "32768,0,0",
            "0,32768,0",
            "0,0,2147483648",
        )
        v2_overflows = (
            "v2,dev01,3,32768,0,0,0,0,0,0,0,0,0,0",
            "v2,dev01,3,0,32768,0,0,0,0,0,0,0,0,0",
            "v2,dev01,3,0,0,2147483648,0,0,0,0,0,0,0,0",
            "v2,dev01,3,0,0,0,32768,0,0,0,0,0,0,0",
            "v2,dev01,3,0,0,0,0,32768,0,0,0,0,0,0",
            "v2,dev01,3,0,0,0,0,0,2147483648,0,0,0,0,0",
            "v2,dev01,3,0,0,0,0,0,0,2147483648,0,0,0,0",
            "v2,dev01,3,0,0,0,0,0,0,0,2147483648,0,0,0",
            "v2,dev01,3,0,0,0,0,0,0,0,0,2147483648,0,0",
            "v2,dev01,3,0,0,0,0,0,0,0,0,0,2147483648,0",
            "v2,dev01,3,0,0,0,0,0,0,0,0,0,0,32768",
        )
        for line in legacy_overflows + v2_overflows:
            with self.subTest(line=line):
                self.assertIsNone(telemetry.parse_line(line))

    def test_parse_line_accepts_sql_integer_edges_and_ignores_invalid_groups(self):
        edge_line = (
            "v2,dev01,3,-32768,32767,2147483647,-32768,32767,"
            "-2147483648,2147483647,-2147483648,2147483647,-2147483648,32767")
        self.assertIsNotNone(telemetry.parse_line(edge_line))
        self.assertIsNotNone(telemetry.parse_line("-32768,32767,2147483647"))
        self.assertIsNotNone(telemetry.parse_line(
            "v2,dev01,1,0,0,0,32768,32768,2147483648,2147483648,2147483648,2147483648,2147483648,32768"))


class FakeCursor:
    def __init__(self, columns=(), non_nullable_columns=(), row=None):
        self.columns = {name: "YES" for name in columns}
        self.columns.update({name: "NO" for name in non_nullable_columns})
        self.row = row
        self.executed = []

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        return False

    def execute(self, sql, params=None):
        normalized = " ".join(sql.split())
        self.executed.append((normalized, params))
        if normalized.startswith("ALTER TABLE sensor_data ADD COLUMN"):
            self.columns[normalized.split()[5]] = "YES"
        if normalized.startswith("ALTER TABLE sensor_data MODIFY COLUMN"):
            self.columns[normalized.split()[5]] = "YES"

    def fetchall(self):
        return [(name, is_nullable) for name, is_nullable in self.columns.items()]

    def fetchone(self):
        return self.row


class FakeConnection:
    def __init__(self, cursor):
        self.cursor_value = cursor
        self.commits = 0
        self.closed = False

    def cursor(self):
        return self.cursor_value

    def commit(self):
        self.commits += 1

    def close(self):
        self.closed = True


class FakeHttpResponse:
    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        return False

    def read(self):
        return b"jpeg"


class StorageTests(unittest.TestCase):
    def test_fetch_recent_retains_offline_probe_last_valid_sample(self):
        stamp = datetime(2026, 9, 23, 10, 0, 0)
        def row(row_id, device, soil_temp):
            return (row_id, device, 250 if device == "tas01" else None,
                    500 if device == "tas01" else None,
                    300 if device == "tas01" else None,
                    1 if soil_temp is None else 2,
                    soil_temp, None, None, None, None, None, None, None, stamp)

        class HistoryCursor(FakeCursor):
            def execute(self, sql, params=None):
                super().execute(sql, params)
                if "device_id NOT IN" in sql:
                    self.result = []
                elif "device_id=%s" in sql and "soil_temp_x10 IS NOT NULL" in sql:
                    self.result = [row(2, "soil02", 220)] if params[0] == "soil02" else [row(8, "soil03", 270)]
                elif "device_id=%s" in sql and "temp_x10 IS NOT NULL" in sql:
                    self.result = [row(7, "tas01", None)]
                elif params[0] == "tas01":
                    self.result = [row(7, "tas01", None)]
                elif params[0] == "soil02":
                    self.result = [row(5, "soil02", None), row(4, "soil02", None)]
                elif "device_id=%s" in sql:
                    self.result = [row(8, "soil03", 270)]
                else:
                    self.result = []

            def fetchall(self):
                return self.result

        cursor = HistoryCursor()
        with patch.object(storage, "get_conn", return_value=FakeConnection(cursor)):
            records = storage.fetch_recent(2)

        self.assertEqual([record[0] for record in records],
                         ["soil03", "tas01", "soil02", "soil02", "soil02"])
        self.assertEqual([record[5] for record in records], [270, None, None, None, 220])
        self.assertEqual(len(cursor.executed), 7)
        self.assertEqual(cursor.executed[0][1], ("tas01", 2))
        self.assertEqual(cursor.executed[1][1], ("soil02", 2))
        self.assertEqual(cursor.executed[2][1], ("soil03", 2))

    def test_save_row_inserts_named_record_with_null_soil_values(self):
        cursor = FakeCursor()
        conn = FakeConnection(cursor)
        record = telemetry.TelemetryRecord(
            "dev01", 1, 268, 391, 440,
            None, None, None, None, None, None, None, None)

        with patch.object(storage, "get_conn", return_value=conn):
            storage.save_row(record)

        sql, params = cursor.executed[0]
        self.assertIn("valid_mask", sql)
        self.assertIn("soil_ph_x10", sql)
        self.assertEqual(
            params,
            ("dev01", 1, 268, 391, 440,
             None, None, None, None, None, None, None, None))
        self.assertEqual(conn.commits, 1)

    def test_save_row_inserts_all_v2_measurements(self):
        cursor = FakeCursor()
        conn = FakeConnection(cursor)
        record = telemetry.parse_line(
            "v2,dev01,3,268,612,1240,243,387,786,392,42,31,58,66")

        with patch.object(storage, "get_conn", return_value=conn):
            storage.save_row(record)

        self.assertEqual(
            cursor.executed[0][1],
            ("dev01", 3, 268, 612, 1240,
             243, 387, 786, 392, 42, 31, 58, 66))

    def test_save_row_keeps_masked_air_fields_null_for_soil_only_record(self):
        cursor = FakeCursor()
        conn = FakeConnection(cursor)
        record = telemetry.parse_line(
            "v2,dev01,2,0,0,0,243,387,786,392,42,31,58,66")

        with patch.object(storage, "get_conn", return_value=conn):
            storage.save_row(record)

        self.assertEqual(
            cursor.executed[0][1],
            ("dev01", 2, None, None, None,
             243, 387, 786, 392, 42, 31, 58, 66))

    def test_init_db_adds_only_missing_telemetry_columns(self):
        database_cursor = FakeCursor()
        sensor_cursor = FakeCursor(
            columns=("id", "device_id", "temp_x10", "humi_x10", "lux", "ts"),
            non_nullable_columns=("temp_x10", "humi_x10", "lux"))
        photo_cursor = FakeCursor()
        database_conn = FakeConnection(database_cursor)
        sensor_conn = FakeConnection(sensor_cursor)
        photo_conn = FakeConnection(photo_cursor)

        with patch.object(storage, "MYSQL_PASSWORD", "test-password"), \
             patch.object(storage.pymysql, "connect", return_value=database_conn), \
             patch.object(storage, "get_conn", side_effect=(
                 sensor_conn, photo_conn, sensor_conn, photo_conn)):
            storage.init_db()
            storage.init_db()

        alters = [sql for sql, _ in sensor_cursor.executed
                  if sql.startswith("ALTER TABLE sensor_data ADD COLUMN")]
        modifies = [sql for sql, _ in sensor_cursor.executed
                    if sql.startswith("ALTER TABLE sensor_data MODIFY COLUMN")]
        self.assertEqual(len(alters), 9)
        self.assertEqual(
            {sql.split()[5] for sql in alters},
            {"valid_mask", "soil_temp_x10", "soil_humi_x10", "soil_ec",
             "soil_salinity", "soil_n", "soil_p", "soil_k", "soil_ph_x10"})
        self.assertEqual(
            {sql.split()[5] for sql in modifies},
            {"temp_x10", "humi_x10", "lux"})

    def test_init_db_does_not_alter_when_columns_are_already_present(self):
        all_columns = {
            "id", "device_id", "temp_x10", "humi_x10", "lux", "ts",
            "valid_mask", "soil_temp_x10", "soil_humi_x10", "soil_ec",
            "soil_salinity", "soil_n", "soil_p", "soil_k", "soil_ph_x10",
        }
        database_cursor = FakeCursor()
        sensor_cursor = FakeCursor(columns=all_columns)
        photo_cursor = FakeCursor()
        database_conn = FakeConnection(database_cursor)
        sensor_conn = FakeConnection(sensor_cursor)
        photo_conn = FakeConnection(photo_cursor)

        with patch.object(storage, "MYSQL_PASSWORD", "test-password"), \
             patch.object(storage.pymysql, "connect", return_value=database_conn), \
             patch.object(storage, "get_conn", side_effect=(sensor_conn, photo_conn)):
            storage.init_db()

        self.assertFalse(any(sql.startswith("ALTER TABLE sensor_data")
                             for sql, _ in sensor_cursor.executed))

    def test_do_snap_uses_photo_sentinel_for_soil_only_latest_reading(self):
        cursor = FakeCursor(row=(None, None, None))
        conn = FakeConnection(cursor)
        response = FakeHttpResponse()

        with patch.dict(camera.CAM_IP, {"cam1": "127.0.0.1"}, clear=True), \
             patch("urllib.request.urlopen", return_value=response), \
             patch.object(camera, "get_conn", return_value=conn), \
             patch.object(camera, "photo_save", return_value="photos/snap.jpg") as photo_save:
            path, error = camera.do_snap("cam1")

        self.assertEqual((path, error), ("photos/snap.jpg", None))
        self.assertEqual(photo_save.call_args.args[1:4], (-1, -1, -1))

    def test_save_snapshot_uses_photo_sentinel_for_soil_only_latest_reading(self):
        cursor = FakeCursor(row=(None, None, None))
        conn = FakeConnection(cursor)

        with patch.object(camera, "get_latest_frame", return_value=(b"jpeg", datetime.now())), \
             patch.object(camera, "get_conn", return_value=conn), \
             patch.object(camera, "photo_save", return_value="photos/snap.jpg") as photo_save:
            path = camera.save_snapshot("cam1")

        self.assertEqual(path, "photos/snap.jpg")
        self.assertEqual(photo_save.call_args.args[1:4], (-1, -1, -1))

    def test_save_snapshot_selects_latest_valid_air_not_second_soil(self):
        cursor = FakeCursor(row=(268, 391, 440))
        conn = FakeConnection(cursor)

        with patch.object(camera, "get_latest_frame", return_value=(b"jpeg", datetime.now())), \
             patch.object(camera, "get_conn", return_value=conn), \
             patch.object(camera, "photo_save", return_value="photos/snap.jpg") as photo_save:
            self.assertEqual(camera.save_snapshot("cam1"), "photos/snap.jpg")

        sql, params = cursor.executed[0]
        self.assertIn("device_id=%s", sql)
        self.assertIn("temp_x10 IS NOT NULL", sql)
        self.assertEqual(params, ("tas01",))
        self.assertEqual(photo_save.call_args.args[1:4], (268, 391, 440))


class WebDisplayTests(unittest.TestCase):
    def test_recent_record_labels_only_the_devices_with_valid_data(self):
        self.assertEqual(dashboard.dashboard_device_label("tas01", 1),
                         "塔石传感器 (tas01 · 0x01)")
        self.assertEqual(dashboard.dashboard_device_label("soil02", 2),
                         "土壤传感1 (soil02 · 0x02)")
        self.assertEqual(dashboard.dashboard_device_label("soil03", 2),
                         "土壤传感2 (soil03 · 0x03)")

    def test_index_labels_three_devices_and_embeds_second_soil_history(self):
        second = ("soil03", None, None, None, 2,
                  270, 650, 900, 200, 40, 50, 60, 62,
                  datetime(2026, 9, 23, 10, 0, 1))
        with patch.object(web, "fetch_recent", return_value=[second]):
            page = web.app.test_client().get("/").get_data(as_text=True)

        self.assertIn("塔石传感器", page)
        self.assertIn("土壤传感1", page)
        self.assertIn("土壤传感2", page)
        self.assertIn("soil2_points: [[", page)
        self.assertNotIn("@@SOIL2_POINTS@@", page)

    def test_soil_selector_uses_each_probe_without_mixing_points(self):
        node = os.environ.get("PLANT_NET_NODE") or shutil.which("node")
        if not node:
            self.skipTest("Node.js is required for probe selector regression")
        script = r'''
const fs=require("fs");
const source=fs.readFileSync("Flask_TCP/plant_server/static/js/dashboard.js","utf8");
const start=source.indexOf("function activeSoilData");
const end=source.indexOf("function setSoilReading");
if(start<0||end<0)throw new Error("probe selector missing");
eval(source.slice(start,end));
global.MAX_SAMPLE_GAP_SECONDS=600;
global.SOIL_DATA=[[100,210,320],[130,220,330]];
global.SOIL2_DATA=[[101,270,650],[800,280,660]];
global.ACTIVE_SOIL=1;
if(JSON.stringify(soilSeries(2,10).values)!=="[32,33]")throw new Error("probe1 mixed");
global.ACTIVE_SOIL=2;
const series=soilSeries(2,10);
if(JSON.stringify(series.values)!=="[65,null,66]")throw new Error("probe2 gap/mixing: "+JSON.stringify(series));
'''
        result = subprocess.run(
            [node, "-e", script], cwd=os.path.dirname(SERVER_DIR),
            text=True, encoding="utf-8", capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_live_table_labels_follow_valid_mask(self):
        node = os.environ.get("PLANT_NET_NODE") or shutil.which("node")
        if not node:
            self.skipTest("Node.js is required for table label regression")
        script = r'''
const fs=require("fs");
const source=fs.readFileSync("Flask_TCP/plant_server/static/js/dashboard.js","utf8");
eval(source.slice(source.indexOf("function deviceLabel"),source.indexOf("function renderRows")));
if(deviceLabel("tas01",1)!=="塔石传感器 (tas01 · 0x01)")throw new Error("air label");
if(deviceLabel("soil02",2)!=="土壤传感1 (soil02 · 0x02)")throw new Error("soil1 label");
if(deviceLabel("soil03",2)!=="土壤传感2 (soil03 · 0x03)")throw new Error("soil2 label");
'''
        result = subprocess.run(
            [node, "-e", script], cwd=os.path.dirname(SERVER_DIR),
            text=True, encoding="utf-8", capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_soil1_failure_shows_last_successful_sample_time(self):
        node = os.environ.get("PLANT_NET_NODE") or shutil.which("node")
        if not node:
            self.skipTest("Node.js is required for missing soil status regression")
        script = r'''
const fs=require("fs");
const source=fs.readFileSync("Flask_TCP/plant_server/static/js/dashboard.js","utf8");
const start=source.indexOf("function activeSoilData");
const end=source.indexOf("/* 将同单位的 N/P/K");
const nodes={};
global.document={getElementById:function(id){return nodes[id]||(nodes[id]={textContent:""});}};
global.fmtDateTime=function(epoch){return "T"+epoch;};
global.SOIL_DATA=[[100,210,320,450,100,10,20,30,70],
                  [130,null,null,null,null,null,null,null,null]];
global.SOIL2_DATA=[];global.ACTIVE_SOIL=1;
eval(source.slice(source.indexOf("function lastValidSample"),source.indexOf("/* 读数卡片")));
eval(source.slice(start,end));
renderSoilHead();
if(nodes.vSoilTemp.textContent!=="21.0")throw new Error("last valid value lost");
if(nodes.nSoilTemp.textContent!=="最近有效采集 T100")
  throw new Error("last valid sample lost: "+nodes.nSoilTemp.textContent);
'''
        result = subprocess.run(
            [node, "-e", script], cwd=os.path.dirname(SERVER_DIR),
            text=True, encoding="utf-8", capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_latest_keeps_both_soil_probes_separate(self):
        stamp1 = datetime(2026, 9, 23, 10, 0, 0)
        stamp2 = datetime(2026, 9, 23, 10, 0, 1)
        air = ("tas01", 250, 500, 300, 1,
               None, None, None, None, None, None, None, None, stamp1)
        first = ("soil02", None, None, None, 2,
                 210, 320, 450, 100, 10, 20, 30, 70, stamp1)
        second = ("soil03", None, None, None, 2,
                  270, 650, 900, 200, 40, 50, 60, 62, stamp2)

        with patch.object(web, "fetch_recent", return_value=[second, first, air]):
            payload = web.app.test_client().get("/api/latest").get_json()

        self.assertEqual(payload["points"], [[int(stamp1.timestamp()), 250, 500, 300]])
        self.assertEqual(payload["soil_points"],
                         [[int(stamp1.timestamp()), 210, 320, 450, 100,
                           10, 20, 30, 70]])
        self.assertEqual(payload["soil2_points"],
                         [[int(stamp2.timestamp()), 270, 650, 900, 200,
                           40, 50, 60, 62]])
        self.assertEqual([row["device"] for row in payload["rows"]],
                         ["soil03", "soil02", "tas01"])

    def test_cam_air_record_does_not_erase_tower_or_soil1_display(self):
        stamp1 = datetime(2026, 9, 23, 10, 0, 0)
        stamp2 = datetime(2026, 9, 23, 10, 0, 1)
        tower = ("tas01", 250, 500, 300, 1,
                 None, None, None, None, None, None, None, None, stamp1)
        soil1 = ("soil02", None, None, None, 2,
                 210, 320, 450, 100, 10, 20, 30, 70, stamp1)
        camera = ("cam1", 280, 600, 700, 1,
                  None, None, None, None, None, None, None, None, stamp2)

        with patch.object(web, "fetch_recent", return_value=[camera, soil1, tower]):
            payload = web.app.test_client().get("/api/latest").get_json()

        self.assertEqual(payload["points"], [[int(stamp1.timestamp()), 250, 500, 300]])
        self.assertEqual(payload["soil_points"],
                         [[int(stamp1.timestamp()), 210, 320, 450, 100,
                           10, 20, 30, 70]])
        self.assertEqual([row["device"] for row in payload["rows"]],
                         ["cam1", "soil02", "tas01"])

    def test_tower_cards_keep_last_valid_sample(self):
        node = os.environ.get("PLANT_NET_NODE") or shutil.which("node")
        if not node:
            self.skipTest("Node.js is required for the dashboard stale-state regression")
        script = r'''
const fs=require("fs");
const source=fs.readFileSync("Flask_TCP/plant_server/static/js/dashboard.js","utf8");
const nodes={};
global.document={getElementById:function(id){return nodes[id]||(nodes[id]={className:"",textContent:""});}};
global.location={protocol:"file:"};
global.DATA=[[100,250,500,300]];
Date.now=function(){return 200000;};
eval(source.slice(source.indexOf("function fmtHM"),source.indexOf("function niceTicks")));
renderHead();
if(nodes.vTemp.textContent!=="25.0"||nodes.vHumi.textContent!=="50.0"||nodes.vLux.textContent!==300)throw new Error("last tower values lost");
if(!nodes.nTemp.textContent.includes("最近有效采集"))throw new Error("tower sample time missing");
'''
        result = subprocess.run(
            [node, "-e", script], cwd=os.path.dirname(SERVER_DIR),
            text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_air_chart_keeps_null_samples_and_breaks_the_path(self):
        node = os.environ.get("PLANT_NET_NODE") or shutil.which("node")
        if not node:
            self.skipTest("Node.js is required for the dashboard chart regression")
        script = r'''
const fs=require("fs");
const source=fs.readFileSync("Flask_TCP/plant_server/static/js/dashboard.js","utf8");
let ops=[],tracking=false;
const ctx={
  set strokeStyle(value){tracking=value==="#test";},get strokeStyle(){return "";},
  setTransform:function(){},clearRect:function(){},fillText:function(){},
  beginPath:function(){if(tracking)ops.push("begin");},moveTo:function(){if(tracking)ops.push("move");},
  lineTo:function(){if(tracking)ops.push("line");},stroke:function(){if(tracking)ops.push("stroke");},arc:function(){},fill:function(){}
};
global.window={devicePixelRatio:1};
global.document={getElementById:function(){return {parentElement:{clientWidth:200},getContext:function(){return ctx;}}}};
function fmtHM(){return "";}
eval(source.slice(source.indexOf("function niceTicks"),source.indexOf("/* 空状态 */")));
eval(source.slice(source.indexOf("function chartSeries"),source.indexOf("function soilSeries")));
global.DATA=[[1,200],[2,null],[3,220]];
const series=chartSeries(1,10);
drawSeries("chart",series.values,series.eps,"#test");
if(JSON.stringify(series)!=='{"eps":[1,2,3],"values":[20,null,22]}'||ops.filter(function(op){return op==="move";}).length!==2||ops.includes("line"))throw new Error("air gap bridged: "+JSON.stringify({series:series,ops:ops}));
'''
        result = subprocess.run(
            [node, "-e", script], cwd=os.path.dirname(SERVER_DIR),
            text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_tab_keyboard_navigation_updates_roving_state_and_panels(self):
        node = os.environ.get("PLANT_NET_NODE") or shutil.which("node")
        if not node:
            self.skipTest("Node.js is required for the dashboard keyboard regression")
        script = r'''
const fs=require("fs");
const source=fs.readFileSync("Flask_TCP/plant_server/static/js/dashboard.js","utf8");
const tabs=["air","soil","nutrients"].map(function(name){return {
  dataset:{panel:name},attrs:{"aria-selected":name==="air"?"true":"false",tabindex:name==="air"?"0":"-1"},handlers:{},
  setAttribute:function(key,value){this.attrs[key]=String(value);},
  getAttribute:function(key){return this.attrs[key];},
  addEventListener:function(type,handler){this.handlers[type]=handler;},
  focus:function(){this.focused=true;}
};});
const panels=["air","soil","nutrients"].map(function(name){return {id:"panel-"+name,hidden:name!=="air"};});
global.document={querySelectorAll:function(selector){return selector===".tab"?tabs:panels;}};
global.drawAll=function(){};
global.ACTIVE_GROUP="air";
const start=source.indexOf("function activatePanel");
const end=source.indexOf("function setLive");
eval(source.slice(start,end));
function press(tab,key){tab.handlers.keydown({key:key,preventDefault:function(){}});}
function check(active){
  tabs.forEach(function(tab,index){const selected=tab.dataset.panel===active; if(tab.getAttribute("aria-selected")!==String(selected)||tab.getAttribute("tabindex")!==(selected?"0":"-1"))throw new Error("bad tab state");});
  panels.forEach(function(panel){if(panel.hidden===(panel.id==="panel-"+active))throw new Error("bad panel state");});
  const activeTab=tabs.filter(function(tab){return tab.dataset.panel===active;})[0];if(!activeTab.focused)throw new Error("active tab was not focused");
}
press(tabs[0],"ArrowRight");check("soil");
press(tabs[1],"End");check("nutrients");
press(tabs[2],"ArrowRight");check("air");
press(tabs[0],"ArrowLeft");check("nutrients");
press(tabs[2],"Home");check("air");
press(tabs[0],"Home");check("air");
'''
        result = subprocess.run(
            [node, "-e", script], cwd=os.path.dirname(SERVER_DIR),
            text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_latest_keeps_air_fields_and_adds_full_soil_measurements(self):
        full_row = (
            "dev02", 268, 612, 1240, 3,
            243, 387, 786, 392, 42, 31, 58, 66,
            datetime(2026, 9, 22, 10, 30, 45))
        with patch.object(web, "fetch_recent", return_value=[full_row]):
            response = web.app.test_client().get("/api/latest")

        self.assertEqual(response.status_code, 200)
        payload = response.get_json()
        self.assertEqual(payload["points"][0][1:], [268, 612, 1240])
        self.assertEqual(
            payload["rows"][0],
            {
                "device": "dev02", "temp": "26.8", "humi": "61.2",
                "lux": 1240, "time": "09-22 10:30:45", "valid_mask": 3,
                "soil_temp": 24.3, "soil_humi": 38.7, "soil_ec": 786,
                "soil_salinity": 392, "soil_n": 42, "soil_p": 31,
                "soil_k": 58, "soil_ph": 6.6,
            })
        self.assertEqual(
            payload["soil_points"],
            [[full_row[-1].timestamp(), 243, 387, 786, 392, 42, 31, 58, 66]])

    def test_latest_returns_json_null_for_unavailable_soil_measurements(self):
        partial_row = (
            "soil02", None, None, None, 2,
            251, None, None, 401, None, 30, None, None,
            datetime(2026, 9, 22, 10, 31, 0))
        with patch.object(web, "fetch_recent", return_value=[partial_row]):
            response = web.app.test_client().get("/api/latest")

        self.assertEqual(response.status_code, 200)
        payload = response.get_json()
        self.assertEqual(payload["rows"][0], {
            "device": "soil02", "temp": "暂无数据", "humi": "暂无数据",
            "lux": None, "time": "09-22 10:31:00", "valid_mask": 2,
            "soil_temp": 25.1, "soil_humi": None, "soil_ec": None,
            "soil_salinity": 401, "soil_n": None, "soil_p": 30,
            "soil_k": None, "soil_ph": None,
        })
        self.assertEqual(
            payload["soil_points"],
            [[partial_row[-1].timestamp(), 251, None, None, 401, None, 30, None, None]])

    def test_index_keeps_legacy_air_labels_and_renders_group_labels(self):
        with patch.object(web, "fetch_recent", return_value=[]):
            page = web.app.test_client().get("/").get_data(as_text=True)

        self.assertIn("植物生长观测站", page)
        self.assertIn("TEMPERATURE · 温度", page)
        self.assertIn("HUMIDITY · 湿度", page)
        self.assertIn("LIGHT · 光照", page)
        self.assertIn("空气环境", page)
        self.assertIn("两组土壤数据对照", page)
        self.assertIn("两组土壤最近记录", page)

    def test_soil_chart_template_preserves_null_gaps_and_breaks_paths(self):
        with patch.object(web, "fetch_recent", return_value=[]):
            page = web.app.test_client().get("/").get_data(as_text=True)

        script = (Path(SERVER_DIR) / "plant_server/static/js/dashboard.js").read_text(encoding="utf-8")
        self.assertIn("/static/js/dashboard.js", page)
        self.assertIn("eps.push(data[i][0]);values.push(value===null?null:value/scale);",
                      script)
        self.assertIn("var data=activeSoilData();", script)
        self.assertIn("if(values[i2]===null){drawing=false;continue;}", script)

    def test_index_renders_soil_only_row_without_air_formatting_crash(self):
        soil_only_row = ("dev01", None, None, None, datetime.now())
        with patch.object(web, "fetch_recent", return_value=[soil_only_row]):
            response = web.app.test_client().get("/")

        self.assertEqual(response.status_code, 200)
        self.assertIn("暂无数据", response.get_data(as_text=True))

    def test_latest_returns_soil_only_row_without_air_formatting_crash(self):
        soil_only_row = ("dev01", None, None, None, datetime.now())
        with patch.object(web, "fetch_recent", return_value=[soil_only_row]):
            response = web.app.test_client().get("/api/latest")

        self.assertEqual(response.status_code, 200)
        payload = response.get_json()
        self.assertEqual(payload["rows"][0]["temp"], "暂无数据")
        self.assertEqual(payload["rows"][0]["humi"], "暂无数据")
        self.assertIsNone(payload["rows"][0]["lux"])

    def test_air_panel_template_guards_null_before_numeric_operations(self):
        soil_only_row = ("dev01", None, None, None, datetime.now())
        with patch.object(web, "fetch_recent", return_value=[soil_only_row]):
            page = web.app.test_client().get("/").get_data(as_text=True)

        script = (Path(SERVER_DIR) / "plant_server/static/js/dashboard.js").read_text(encoding="utf-8")
        self.assertIn("/static/js/dashboard.js", page)
        self.assertIn("lastValidSample(DATA,item[1])", script)
        self.assertIn('textContent="暂无数据"', script)
        self.assertNotIn("last[1]/10", script)
        self.assertNotIn("last[2]/10", script)
        self.assertNotIn("t.push(DATA[i][1]/10)", script)
        self.assertNotIn("h.push(DATA[i][2]/10)", script)
        self.assertIn("eps.push(DATA[i][0]);values.push(value===null?null:value/scale);", script)
        self.assertIn("chartSeries(1,10)", script)
        self.assertIn("chartSeries(2,10)", script)
        self.assertIn("chartSeries(3,1)", script)


if __name__ == "__main__":
    unittest.main()

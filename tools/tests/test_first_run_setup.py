import sys
import tempfile
import unittest
from pathlib import Path

from tools.first_run_setup import (
    main,
    render_local_config,
    render_network_config,
    write_configuration,
)


NETWORK_TEMPLATE = '''#ifndef PLANT_NET_CONFIG_H
#define PLANT_NET_CONFIG_H
#define PN_WIFI_SSID      "YOUR_WIFI_SSID"
#define PN_WIFI_PASS      "YOUR_WIFI_PASSWORD"
#define PN_SERVER_HOST    "192.168.137.1"
#define PN_DATA_PORT      9000
#define PN_PUSH_PORT      9001
#define PN_HTTP_PORT      8080
#endif
'''

LOCAL_TEMPLATE = '''MYSQL_HOST = "127.0.0.1"
MYSQL_USER = "root"
MYSQL_PASSWORD = "YOUR_MYSQL_PASSWORD"
MYSQL_DB = "plant_db"
'''


def make_project(root: Path) -> None:
    (root / 'config').mkdir()
    (root / 'Flask_TCP').mkdir()
    (root / 'STM32/Core/Inc').mkdir(parents=True)
    (root / 'ESP-CAM/CameraWebServer').mkdir(parents=True)
    (root / 'config/net_config.example.h').write_text(
        NETWORK_TEMPLATE,
        encoding='utf-8',
    )
    (root / 'Flask_TCP/local_config.example.py').write_text(
        LOCAL_TEMPLATE,
        encoding='utf-8',
    )


class RendererTests(unittest.TestCase):
    def test_network_renderer_replaces_only_two_mutable_fields(self):
        source = (
            '#define PN_WIFI_SSID "YOUR_WIFI_SSID"\n'
            '#define PN_WIFI_PASS "YOUR_WIFI_PASSWORD"\n'
            '#define PN_SERVER_HOST "192.168.137.1"\n'
            '#define PN_DATA_PORT 9000\n'
        )

        rendered = render_network_config(source, "PlantHotspot", "pass12345")

        self.assertIn('#define PN_WIFI_SSID "PlantHotspot"', rendered)
        self.assertIn('#define PN_WIFI_PASS "pass12345"', rendered)
        self.assertIn('#define PN_SERVER_HOST "192.168.137.1"', rendered)
        self.assertIn('#define PN_DATA_PORT 9000', rendered)

    def test_network_renderer_preserves_crlf_line_endings(self):
        source = (
            '#define PN_WIFI_SSID      "YOUR_WIFI_SSID"\r\n'
            '#define PN_WIFI_PASS      "YOUR_WIFI_PASSWORD"\r\n'
            '#define PN_SERVER_HOST    "192.168.137.1"\r\n'
        )

        rendered = render_network_config(source, "PlantHotspot", "pass12345")

        self.assertEqual(
            rendered,
            '#define PN_WIFI_SSID      "PlantHotspot"\r\n'
            '#define PN_WIFI_PASS      "pass12345"\r\n'
            '#define PN_SERVER_HOST    "192.168.137.1"\r\n',
        )

    def test_network_renderer_ignores_commented_macros(self):
        source = (
            '// #define PN_WIFI_SSID "commented"\n'
            '#define PN_WIFI_SSID "YOUR_WIFI_SSID"\n'
            '/* #define PN_WIFI_PASS "commented" */\n'
            '#define PN_WIFI_PASS "YOUR_WIFI_PASSWORD"\n'
        )

        rendered = render_network_config(source, "PlantHotspot", "pass12345")

        self.assertIn('// #define PN_WIFI_SSID "commented"', rendered)
        self.assertIn('/* #define PN_WIFI_PASS "commented" */', rendered)
        self.assertIn('#define PN_WIFI_SSID "PlantHotspot"', rendered)
        self.assertIn('#define PN_WIFI_PASS "pass12345"', rendered)

    def test_network_renderer_ignores_macros_inside_multiline_block_comments(self):
        source = (
            '/* Old example:\n'
            '#define PN_WIFI_SSID "commented"\n'
            '*/\n'
            '#define PN_WIFI_SSID "YOUR_WIFI_SSID"\n'
            '#define PN_WIFI_PASS "YOUR_WIFI_PASSWORD"\n'
        )

        rendered = render_network_config(source, "PlantHotspot", "pass12345")

        self.assertIn('#define PN_WIFI_SSID "commented"', rendered)
        self.assertIn('#define PN_WIFI_SSID "PlantHotspot"', rendered)

    def test_network_renderer_does_not_treat_line_comment_delimiters_as_block_comments(self):
        source = (
            '// old marker /*\n'
            '#define PN_WIFI_SSID "YOUR_WIFI_SSID"\n'
            '#define PN_WIFI_PASS "YOUR_WIFI_PASSWORD"\n'
        )

        rendered = render_network_config(source, "PlantHotspot", "pass12345")

        self.assertIn('#define PN_WIFI_SSID "PlantHotspot"', rendered)
        self.assertIn('#define PN_WIFI_PASS "pass12345"', rendered)

    def test_network_renderer_ignores_nested_block_delimiters(self):
        source = (
            '/* Old example:\n'
            '/* This delimiter does not start a nested comment.\n'
            '*/\n'
            '#define PN_WIFI_SSID "YOUR_WIFI_SSID"\n'
            '#define PN_WIFI_PASS "YOUR_WIFI_PASSWORD"\n'
        )

        rendered = render_network_config(source, "PlantHotspot", "pass12345")

        self.assertIn('#define PN_WIFI_SSID "PlantHotspot"', rendered)
        self.assertIn('#define PN_WIFI_PASS "pass12345"', rendered)

    def test_network_renderer_ignores_comment_markers_inside_c_literals(self):
        source = (
            '#define PN_OTHER "/* //"\n'
            "#define PN_MARKER '/'\n"
            '#define PN_WIFI_SSID "YOUR_WIFI_SSID"\n'
            '#define PN_WIFI_PASS "YOUR_WIFI_PASSWORD"\n'
        )

        rendered = render_network_config(source, "PlantHotspot", "pass12345")

        self.assertIn('#define PN_OTHER "/* //"', rendered)
        self.assertIn("#define PN_MARKER '/'", rendered)
        self.assertIn('#define PN_WIFI_SSID "PlantHotspot"', rendered)
        self.assertIn('#define PN_WIFI_PASS "pass12345"', rendered)

    def test_network_renderer_rejects_missing_or_duplicate_target_macros(self):
        missing_ssid = '#define PN_WIFI_PASS "YOUR_WIFI_PASSWORD"\n'
        duplicate_password = (
            '#define PN_WIFI_SSID "YOUR_WIFI_SSID"\n'
            '#define PN_WIFI_PASS "first"\n'
            '#define PN_WIFI_PASS "second"\n'
        )

        with self.assertRaises(ValueError):
            render_network_config(missing_ssid, "PlantHotspot", "pass12345")
        with self.assertRaises(ValueError):
            render_network_config(duplicate_password, "PlantHotspot", "pass12345")

    def test_network_renderer_rejects_unsafe_credential_values(self):
        source = (
            '#define PN_WIFI_SSID "YOUR_WIFI_SSID"\n'
            '#define PN_WIFI_PASS "YOUR_WIFI_PASSWORD"\n'
        )

        for unsafe_value in ('has"quote', 'has\\backslash', 'has\rreturn', 'has\nnewline'):
            with self.subTest(field="ssid", value=unsafe_value):
                with self.assertRaises(ValueError):
                    render_network_config(source, unsafe_value, "pass12345")
            with self.subTest(field="password", value=unsafe_value):
                with self.assertRaises(ValueError):
                    render_network_config(source, "PlantHotspot", unsafe_value)

    def test_mysql_renderer_replaces_placeholder(self):
        source = 'MYSQL_PASSWORD = "YOUR_MYSQL_PASSWORD"\n'

        self.assertEqual(
            render_local_config(source, "mysql-pass"),
            'MYSQL_PASSWORD = "mysql-pass"\n',
        )

    def test_mysql_renderer_rejects_missing_or_duplicate_placeholder(self):
        with self.assertRaises(ValueError):
            render_local_config('MYSQL_USER = "root"\n', "mysql-pass")
        with self.assertRaises(ValueError):
            render_local_config(
                'MYSQL_PASSWORD = "YOUR_MYSQL_PASSWORD"\n'
                'MYSQL_PASSWORD = "YOUR_MYSQL_PASSWORD"\n',
                "mysql-pass",
            )

    def test_mysql_renderer_rejects_unsafe_password(self):
        source = 'MYSQL_PASSWORD = "YOUR_MYSQL_PASSWORD"\n'

        for unsafe_value in ('has"quote', 'has\\backslash', 'has\rreturn', 'has\nnewline'):
            with self.subTest(value=unsafe_value):
                with self.assertRaises(ValueError):
                    render_local_config(source, unsafe_value)


class ConfigurationWriterTests(unittest.TestCase):
    def test_writer_creates_four_private_files_without_changing_templates(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            make_project(root)
            network_template_before = (
                root / 'config/net_config.example.h'
            ).read_bytes()
            local_template_before = (
                root / 'Flask_TCP/local_config.example.py'
            ).read_bytes()

            write_configuration(root, 'db-secret', 'PlantHotspot', 'wifi-secret')

            network_paths = (
                root / 'config/net_config.h',
                root / 'STM32/Core/Inc/net_config.h',
                root / 'ESP-CAM/CameraWebServer/net_config.h',
            )
            local_path = root / 'Flask_TCP/local_config.py'
            for path in (*network_paths, local_path):
                self.assertTrue(path.is_file(), path)

            network_copies = [path.read_bytes() for path in network_paths]
            self.assertEqual(network_copies[0], network_copies[1])
            self.assertEqual(network_copies[0], network_copies[2])
            network_text = network_copies[0].decode('utf-8')
            self.assertIn('#define PN_WIFI_SSID      "PlantHotspot"', network_text)
            self.assertIn('#define PN_WIFI_PASS      "wifi-secret"', network_text)
            self.assertIn('#define PN_SERVER_HOST    "192.168.137.1"', network_text)
            self.assertIn('#define PN_DATA_PORT      9000', network_text)
            self.assertIn('#define PN_PUSH_PORT      9001', network_text)
            self.assertIn('#define PN_HTTP_PORT      8080', network_text)
            self.assertIn(
                'MYSQL_PASSWORD = "db-secret"',
                local_path.read_text(encoding='utf-8'),
            )
            self.assertEqual(
                network_template_before,
                (root / 'config/net_config.example.h').read_bytes(),
            )
            self.assertEqual(
                local_template_before,
                (root / 'Flask_TCP/local_config.example.py').read_bytes(),
            )

    def test_writer_rejects_invalid_inputs_before_creating_any_output(self):
        invalid_cases = (
            ('', 'PlantHotspot', 'wifi-secret'),
            ('db-secret', '   ', 'wifi-secret'),
            ('db-secret', 'PlantHotspot', ''),
            ('bad"db', 'PlantHotspot', 'wifi-secret'),
            ('db-secret', 'bad\\ssid', 'wifi-secret'),
            ('db-secret', 'PlantHotspot', 'bad\npassword'),
        )

        for mysql_password, ssid, wifi_password in invalid_cases:
            with self.subTest(
                mysql_password=mysql_password,
                ssid=ssid,
                wifi_password=wifi_password,
            ), tempfile.TemporaryDirectory() as temp_dir:
                root = Path(temp_dir)
                make_project(root)

                with self.assertRaises(ValueError):
                    write_configuration(root, mysql_password, ssid, wifi_password)

                output_paths = (
                    root / 'config/net_config.h',
                    root / 'STM32/Core/Inc/net_config.h',
                    root / 'ESP-CAM/CameraWebServer/net_config.h',
                    root / 'Flask_TCP/local_config.py',
                )
                self.assertFalse(any(path.exists() for path in output_paths))

    def test_writer_checks_all_required_paths_before_writing(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            make_project(root)
            (root / 'ESP-CAM/CameraWebServer').rmdir()
            (root / 'ESP-CAM').rmdir()

            with self.assertRaisesRegex(OSError, 'ESP-CAM'):
                write_configuration(root, 'db-secret', 'PlantHotspot', 'wifi-secret')

            self.assertFalse((root / 'config/net_config.h').exists())
            self.assertFalse((root / 'STM32/Core/Inc/net_config.h').exists())
            self.assertFalse((root / 'Flask_TCP/local_config.py').exists())


class InteractiveMainTests(unittest.TestCase):
    def test_main_prompts_for_three_values_and_reports_outputs_without_secrets(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            make_project(root)
            hidden_values = iter(('db-ultra-secret', 'wifi-ultra-secret'))
            hidden_prompts = []
            visible_prompts = []
            output = []

            def read_hidden(prompt):
                hidden_prompts.append(prompt)
                return next(hidden_values)

            def read_visible(prompt):
                visible_prompts.append(prompt)
                return 'PlantHotspot'

            result = main(
                root=root,
                input_func=read_visible,
                password_func=read_hidden,
                output_func=output.append,
            )

            self.assertEqual(0, result)
            self.assertEqual(2, len(hidden_prompts))
            self.assertIn('MySQL', hidden_prompts[0])
            self.assertIn('热点密码', hidden_prompts[1])
            self.assertEqual(1, len(visible_prompts))
            self.assertIn('热点名称', visible_prompts[0])

            rendered_output = '\n'.join(output)
            for relative_path in (
                'config/net_config.h',
                'STM32/Core/Inc/net_config.h',
                'ESP-CAM/CameraWebServer/net_config.h',
                'Flask_TCP/local_config.py',
            ):
                self.assertIn(str(root / relative_path), rendered_output)
            self.assertIn('启动 Flask 服务器', rendered_output)
            self.assertIn(str(Path(sys.executable)), rendered_output)
            self.assertIn(str(root / 'Flask_TCP/server.py'), rendered_output)
            self.assertIn('编译并烧录 STM32', rendered_output)
            self.assertIn('上传 ESP-CAM', rendered_output)
            self.assertNotIn('db-ultra-secret', rendered_output)
            self.assertNotIn('wifi-ultra-secret', rendered_output)

    def test_main_catches_expected_errors_and_returns_nonzero(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            make_project(root)
            hidden_values = iter(('', 'unused'))
            output = []

            result = main(
                root=root,
                input_func=lambda _prompt: 'PlantHotspot',
                password_func=lambda _prompt: next(hidden_values),
                output_func=output.append,
            )

            self.assertNotEqual(0, result)
            self.assertTrue(any(line.startswith('[错误]') for line in output))
            self.assertFalse((root / 'config/net_config.h').exists())
            self.assertFalse((root / 'Flask_TCP/local_config.py').exists())

    def test_main_handles_cancelled_or_closed_input(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            make_project(root)
            output = []

            result = main(
                root=root,
                input_func=lambda _prompt: 'PlantHotspot',
                password_func=lambda _prompt: (_ for _ in ()).throw(EOFError()),
                output_func=output.append,
            )

            self.assertNotEqual(0, result)
            self.assertTrue(any(line.startswith('[错误]') for line in output))
            self.assertFalse((root / 'config/net_config.h').exists())
            self.assertFalse((root / 'Flask_TCP/local_config.py').exists())

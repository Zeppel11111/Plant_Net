# First-Run Wizard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Provide a double-click Windows wizard that collects only a MySQL password, hotspot name, and hotspot password, then generates every local configuration file needed before firmware flashing.

**Architecture:** `首次配置.bat` is a minimal Windows Explorer entry point that launches `tools/first_run_setup.py` with the project-local Python interpreter. The Python program owns all prompting, validation, template rendering, and synchronized writes so that the two firmware files cannot drift.

**Tech Stack:** Windows batch, Python standard library (`getpass`, `pathlib`, `shutil`, `re`), `unittest`.

---

### Task 1: Test the local configuration renderer

**Files:**
- Create: `tools/tests/test_first_run_setup.py`
- Create: `tools/first_run_setup.py`

- [ ] **Step 1: Write the failing tests**

```python
import tempfile
import unittest
from pathlib import Path

from tools.first_run_setup import render_local_config, render_network_config, write_configuration


class RendererTests(unittest.TestCase):
    def test_network_renderer_replaces_only_three_user_fields(self):
        source = '#define PN_WIFI_SSID "YOUR_WIFI_SSID"\n#define PN_WIFI_PASS "YOUR_WIFI_PASSWORD"\n#define PN_SERVER_HOST "192.168.137.1"\n#define PN_DATA_PORT 9000\n'
        rendered = render_network_config(source, "PlantHotspot", "pass12345")
        self.assertIn('#define PN_WIFI_SSID "PlantHotspot"', rendered)
        self.assertIn('#define PN_WIFI_PASS "pass12345"', rendered)
        self.assertIn('#define PN_SERVER_HOST "192.168.137.1"', rendered)
        self.assertIn('#define PN_DATA_PORT 9000', rendered)

    def test_mysql_renderer_replaces_placeholder(self):
        source = 'MYSQL_PASSWORD = "YOUR_MYSQL_PASSWORD"\n'
        self.assertEqual(
            render_local_config(source, "mysql-pass"),
            'MYSQL_PASSWORD = "mysql-pass"\n',
        )

    def test_writer_creates_private_files_and_matching_firmware_copies(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            (root / 'config').mkdir()
            (root / 'Flask_TCP').mkdir()
            (root / 'STM32/Core/Inc').mkdir(parents=True)
            (root / 'ESP-CAM/CameraWebServer').mkdir(parents=True)
            (root / 'config/net_config.example.h').write_text('#define PN_WIFI_SSID "YOUR_WIFI_SSID"\n#define PN_WIFI_PASS "YOUR_WIFI_PASSWORD"\n', encoding='utf-8')
            (root / 'Flask_TCP/local_config.example.py').write_text('MYSQL_PASSWORD = "YOUR_MYSQL_PASSWORD"\n', encoding='utf-8')
            write_configuration(root, 'dbpass', 'MyHotspot', 'wifi-pass')
            canonical = (root / 'config/net_config.h').read_text(encoding='utf-8')
            self.assertEqual(canonical, (root / 'STM32/Core/Inc/net_config.h').read_text(encoding='utf-8'))
            self.assertEqual(canonical, (root / 'ESP-CAM/CameraWebServer/net_config.h').read_text(encoding='utf-8'))
            self.assertIn('dbpass', (root / 'Flask_TCP/local_config.py').read_text(encoding='utf-8'))
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `python -m unittest tools.tests.test_first_run_setup -v`

Expected: FAIL because `tools.first_run_setup` does not exist.

- [ ] **Step 3: Implement the minimal renderer functions**

```python
def render_network_config(template: str, ssid: str, password: str) -> str:
    result = re.sub(r'(#define\\s+PN_WIFI_SSID\\s+)"[^"]*"', rf'\\g<1>"{ssid}"', template, count=1)
    return re.sub(r'(#define\\s+PN_WIFI_PASS\\s+)"[^"]*"', rf'\\g<1>"{password}"', result, count=1)


def render_local_config(template: str, mysql_password: str) -> str:
    return template.replace('MYSQL_PASSWORD = "YOUR_MYSQL_PASSWORD"', f'MYSQL_PASSWORD = "{mysql_password}"', 1)
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `python -m unittest tools.tests.test_first_run_setup -v`

Expected: PASS with two renderer tests.

### Task 2: Build the interactive writer and Explorer entry point

**Files:**
- Modify: `.gitignore`
- Modify: `README.md`
- Create: `tools/first_run_setup.py`
- Create: `首次配置.bat`
- Delete: the legacy command-line configuration script

- [ ] **Step 1: Extend the failing test with synchronized output behavior**

The third test method from Task 1 imports `write_configuration`; it will now fail until that function exists.

- [ ] **Step 2: Run the test to verify it fails**

Run: `python -m unittest tools.tests.test_first_run_setup -v`

Expected: FAIL because `write_configuration` is missing.

- [ ] **Step 3: Implement `write_configuration` and `main`**

```python
def write_configuration(root: Path, mysql_password: str, ssid: str, wifi_password: str) -> None:
    network = render_network_config((root / 'config/net_config.example.h').read_text(encoding='utf-8'), ssid, wifi_password)
    for relative_path in ('config/net_config.h', 'STM32/Core/Inc/net_config.h', 'ESP-CAM/CameraWebServer/net_config.h'):
        (root / relative_path).write_text(network, encoding='utf-8', newline='')
    local = render_local_config((root / 'Flask_TCP/local_config.example.py').read_text(encoding='utf-8'), mysql_password)
    (root / 'Flask_TCP/local_config.py').write_text(local, encoding='utf-8', newline='')
```

`main()` must use `input()` for the hotspot fields and `getpass.getpass()` for the MySQL password, reject blank entries and values containing `"`, `\r`, or `\n`, call `write_configuration`, and print the three next steps: start `Flask_TCP/server.py`, build/flash STM32, upload ESP-CAM.

Create `首次配置.bat` containing:

```bat
@echo off
chcp 65001 >nul
cd /d "%~dp0"
py -3 tools\first_run_setup.py
if errorlevel 1 echo. & echo 配置未完成，请根据上方提示处理。
pause
```

Remove the legacy command-line configuration script. Keep the generated `local_config.py` and all three `net_config.h` paths in `.gitignore`.

Replace the root README's old command-line instructions with: “Double-click `首次配置.bat`, fill in the three prompts, then compile/flash the two firmware projects.”

- [ ] **Step 4: Run all wizard tests**

Run: `python -m unittest tools.tests.test_first_run_setup -v`

Expected: PASS with the renderer and synchronized-copy tests.

- [ ] **Step 5: Manually verify the Windows entry point**

In Explorer, double-click `首次配置.bat`, enter three non-empty test values, and verify that all four generated files exist. Do not compile or flash firmware; those remain the user's task.

"""Render local first-run configuration templates."""

import getpass
import re
import sys
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parent.parent
NETWORK_OUTPUTS = (
    Path('config/net_config.h'),
    Path('STM32/Core/Inc/net_config.h'),
    Path('ESP-CAM/CameraWebServer/net_config.h'),
)
LOCAL_OUTPUT = Path('Flask_TCP/local_config.py')


def _validate_credential(value: str) -> None:
    if not isinstance(value, str) or not value.strip():
        raise ValueError('配置值不能为空。')
    if any(character in value for character in ('"', '\\', '\r', '\n')):
        raise ValueError('配置值不能包含双引号、反斜杠或换行符。')


def _live_line_starts(template: str) -> set[int]:
    """Return starts of lines that begin outside C-style block comments."""
    live_starts = set()
    in_block_comment = False
    in_string = False
    in_character = False
    escaped = False
    line_start = True
    index = 0

    while index < len(template):
        character = template[index]
        next_character = template[index + 1] if index + 1 < len(template) else ''

        if line_start and not (in_block_comment or in_string or in_character):
            live_starts.add(index)
        line_start = character == '\n'

        if in_block_comment:
            if character == '*' and next_character == '/':
                in_block_comment = False
                index += 2
                continue
        elif in_string or in_character:
            if escaped:
                escaped = False
            elif character == '\\':
                escaped = True
            elif (in_string and character == '"') or (in_character and character == "'"):
                in_string = False
                in_character = False
        elif character == '/' and next_character == '/':
            newline = template.find('\n', index + 2)
            if newline == -1:
                break
            index = newline
            continue
        elif character == '/' and next_character == '*':
            in_block_comment = True
            index += 2
            continue
        elif character == '"':
            in_string = True
        elif character == "'":
            in_character = True

        index += 1

    return live_starts


def _replace_network_macro(template: str, macro: str, value: str) -> str:
    pattern = re.compile(
        rf'^(?P<prefix>[ \t]*#define[ \t]+{macro}[ \t]+)"[^"\r\n]*"'
        r'(?P<suffix>[^\r\n]*)(?P<line_ending>\r?)$',
        re.MULTILINE,
    )
    live_line_starts = _live_line_starts(template)
    matches = [match for match in pattern.finditer(template) if match.start() in live_line_starts]
    if len(matches) != 1:
        raise ValueError(f'Expected exactly one {macro} definition in the template.')

    target_start = matches[0].start()
    return pattern.sub(
        lambda match: (
            f'{match.group("prefix")}"{value}"{match.group("suffix")}{match.group("line_ending")}'
            if match.start() == target_start
            else match.group(0)
        ),
        template,
    )


def render_network_config(template: str, ssid: str, password: str) -> str:
    """Insert Wi-Fi credentials into a network configuration template."""
    _validate_credential(ssid)
    _validate_credential(password)
    result = _replace_network_macro(template, 'PN_WIFI_SSID', ssid)
    return _replace_network_macro(result, 'PN_WIFI_PASS', password)


def render_local_config(template: str, mysql_password: str) -> str:
    """Insert the MySQL password into a local configuration template."""
    _validate_credential(mysql_password)
    placeholder = 'MYSQL_PASSWORD = "YOUR_MYSQL_PASSWORD"'
    pattern = re.compile(re.escape(placeholder))
    if len(pattern.findall(template)) != 1:
        raise ValueError('Expected exactly one MySQL password placeholder in the template.')
    return pattern.sub(lambda _match: f'MYSQL_PASSWORD = "{mysql_password}"', template, count=1)


def _read_text(path: Path) -> str:
    with path.open('r', encoding='utf-8', newline='') as source:
        return source.read()


def _write_text(path: Path, content: str) -> None:
    with path.open('w', encoding='utf-8', newline='') as destination:
        destination.write(content)


def write_configuration(
    root: Path,
    mysql_password: str,
    ssid: str,
    wifi_password: str,
) -> None:
    """Generate the server and synchronized firmware configuration files."""
    for value in (mysql_password, ssid, wifi_password):
        _validate_credential(value)

    root = Path(root)
    network_template_path = root / 'config/net_config.example.h'
    local_template_path = root / 'Flask_TCP/local_config.example.py'
    network_output_paths = tuple(root / path for path in NETWORK_OUTPUTS)
    local_output_path = root / LOCAL_OUTPUT

    for template_path in (network_template_path, local_template_path):
        if not template_path.is_file():
            raise OSError(f'缺少配置模板：{template_path}')
    for output_path in (*network_output_paths, local_output_path):
        if not output_path.parent.is_dir():
            raise OSError(f'缺少目标目录：{output_path.parent}')

    network_content = render_network_config(
        _read_text(network_template_path),
        ssid,
        wifi_password,
    )
    local_content = render_local_config(
        _read_text(local_template_path),
        mysql_password,
    )

    for output_path in network_output_paths:
        _write_text(output_path, network_content)
    _write_text(local_output_path, local_content)


def main(
    root=None,
    *,
    input_func=None,
    password_func=None,
    output_func=print,
) -> int:
    """Run the three-question first-time setup wizard."""
    root = PROJECT_ROOT if root is None else Path(root)
    input_func = input if input_func is None else input_func
    password_func = getpass.getpass if password_func is None else password_func

    try:
        output_func('=== Plant_Net 首次配置 ===')
        mysql_password = password_func('请输入 MySQL root 密码（输入不回显）：')
        ssid = input_func('请输入 Windows 移动热点名称：')
        wifi_password = password_func('请输入 Windows 移动热点密码（输入不回显）：')
        write_configuration(root, mysql_password, ssid, wifi_password)
    except (ValueError, OSError, EOFError, KeyboardInterrupt) as error:
        output_func(f'[错误] {error}')
        return 1

    output_func('')
    output_func('[完成] 已生成以下本机配置文件：')
    for relative_path in (*NETWORK_OUTPUTS, LOCAL_OUTPUT):
        output_func(f'  {root / relative_path}')
    output_func('')
    output_func('下一步：')
    output_func(
        f'  1. 启动 Flask 服务器："{sys.executable}" '
        f'"{root / "Flask_TCP/server.py"}"'
    )
    output_func('  2. 使用 STM32CubeIDE 或 CMake 编译并烧录 STM32 固件。')
    output_func(
        '  3. 使用 Arduino IDE 上传 ESP-CAM 固件：'
        'ESP-CAM/CameraWebServer/CameraWebServer.ino'
    )
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

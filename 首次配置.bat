@echo off
setlocal EnableExtensions EnableDelayedExpansion
chcp 65001 >nul
cd /d "%~dp0"

if exist "Flask_TCP\.venv\Scripts\python.exe" (
    "Flask_TCP\.venv\Scripts\python.exe" "tools\first_run_setup.py"
    set "exit_code=!errorlevel!"
    goto :finished
)

where py >nul 2>nul
if not errorlevel 1 (
    py -3 "tools\first_run_setup.py"
    set "exit_code=!errorlevel!"
    goto :finished
)

where python >nul 2>nul
if not errorlevel 1 (
    python "tools\first_run_setup.py"
    set "exit_code=!errorlevel!"
    goto :finished
)

echo [错误] 未找到可用的 Python。请安装 Python 3 后重新双击“首次配置.bat”。
set "exit_code=1"

:finished
if not "!exit_code!"=="0" (
    echo.
    echo 配置未完成，请根据上方提示处理。
)
echo.
pause
exit /b !exit_code!

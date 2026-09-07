@echo off
setlocal
pushd "%~dp0..\.."

set "PYTHON=%USERPROFILE%\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
if not exist "%PYTHON%" (
    where python >nul 2>&1
    if errorlevel 1 (
        echo Python 3 was not found.
        popd
        exit /b 1
    )
    set "PYTHON=python"
)

if not exist "tools\game_mcp\.venv\Scripts\python.exe" (
    "%PYTHON%" -m venv tools\game_mcp\.venv
    if errorlevel 1 (
        popd
        exit /b 1
    )
)

"tools\game_mcp\.venv\Scripts\python.exe" -m pip install -r tools\game_mcp\requirements.txt
set "EXIT_CODE=%ERRORLEVEL%"
popd
exit /b %EXIT_CODE%

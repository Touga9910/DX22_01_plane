@echo off
setlocal
pushd "%~dp0..\.."

set "PYTHON=tools\game_mcp\.venv\Scripts\python.exe"
if not exist "%PYTHON%" (
    echo MCP environment is not installed.
    echo Run tools\game_mcp\setup_game_mcp.cmd first.
    popd
    exit /b 1
)

"%PYTHON%" tools\game_mcp\server.py %*
set "EXIT_CODE=%ERRORLEVEL%"
popd
exit /b %EXIT_CODE%

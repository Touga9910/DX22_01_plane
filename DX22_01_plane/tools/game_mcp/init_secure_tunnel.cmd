@echo off
setlocal

where tunnel-client >nul 2>&1
if errorlevel 1 (
    echo tunnel-client was not found.
    echo Download it from OpenAI Platform tunnel settings.
    exit /b 1
)

if "%CONTROL_PLANE_API_KEY%"=="" (
    echo CONTROL_PLANE_API_KEY is not set.
    exit /b 1
)

if "%GAME_MCP_TUNNEL_ID%"=="" (
    echo GAME_MCP_TUNNEL_ID is not set.
    exit /b 1
)

tunnel-client init ^
    --sample sample_mcp_remote_no_auth ^
    --profile dx22-game ^
    --tunnel-id "%GAME_MCP_TUNNEL_ID%" ^
    --mcp-server-url "http://127.0.0.1:8765/mcp"
if errorlevel 1 exit /b 1

tunnel-client doctor --profile dx22-game --explain
exit /b %ERRORLEVEL%

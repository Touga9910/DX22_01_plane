@echo off
setlocal

where tunnel-client >nul 2>&1
if errorlevel 1 (
    echo tunnel-client was not found.
    exit /b 1
)

if "%CONTROL_PLANE_API_KEY%"=="" (
    echo CONTROL_PLANE_API_KEY is not set.
    exit /b 1
)

tunnel-client run --profile dx22-game
exit /b %ERRORLEVEL%

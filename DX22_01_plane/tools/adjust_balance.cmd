@echo off
setlocal
pushd "%~dp0.."

python --version >nul 2>&1
if not errorlevel 1 (
    python tools\generate_balance_plan_with_ai.py %*
    set EXIT_CODE=%ERRORLEVEL%
    popd
    exit /b %EXIT_CODE%
)

py -3 --version >nul 2>&1
if not errorlevel 1 (
    py -3 tools\generate_balance_plan_with_ai.py %*
    set EXIT_CODE=%ERRORLEVEL%
    popd
    exit /b %EXIT_CODE%
)

set "BUNDLED_PYTHON=%USERPROFILE%\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
if exist "%BUNDLED_PYTHON%" (
    "%BUNDLED_PYTHON%" tools\generate_balance_plan_with_ai.py %*
    set EXIT_CODE=%ERRORLEVEL%
    popd
    exit /b %EXIT_CODE%
)

echo Python 3 was not found.
popd
exit /b 1

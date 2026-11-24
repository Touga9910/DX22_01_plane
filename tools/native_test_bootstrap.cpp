// A failed console regression must report to its log, never wait for a modal.
#include <Windows.h>
#include <crtdbg.h>
#include <cstdlib>

namespace
{
struct ConsoleFailureReports final
{
    /// <summary>
    /// ネイティブテストの異常終了やアサーションの報告先を標準エラーに設定する。
    /// </summary>
    ConsoleFailureReports()
    {
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        _set_error_mode(_OUT_TO_STDERR);
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
        _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    }
} consoleFailureReports;
}

#include "ApplicationLifecycle.h"

#include <cstdio>

namespace
{
    SRWLOCK s_WriteLock = SRWLOCK_INIT;
    volatile LONG s_HiddenWindow = 0;
    alignas(8) volatile LONG64 s_Sequence = 0;

    struct DisplayMode
    {
        UINT width;
        UINT height;
        bool fullscreen;
    };

    /// <summary>
    /// 特殊文字をエスケープし、文字列をJSONの値としてファイルに書き込む。
    /// </summary>
    void WriteJsonString(FILE* file, const char* value) noexcept
    {
        std::fputc('"', file);
        if (value != nullptr)
        {
            for (const unsigned char* character =
                    reinterpret_cast<const unsigned char*>(value);
                *character != 0;
                ++character)
            {
                switch (*character)
                {
                case '"': std::fputs("\\\"", file); break;
                case '\\': std::fputs("\\\\", file); break;
                case '\n': std::fputs("\\n", file); break;
                case '\r': std::fputs("\\r", file); break;
                case '\t': std::fputs("\\t", file); break;
                default:
                    if (*character < 0x20)
                        std::fprintf(file, "\\u%04x", static_cast<unsigned int>(*character));
                    else
                        std::fputc(*character, file);
                    break;
                }
            }
        }
        std::fputc('"', file);
    }

    /// <summary>
    /// アプリケーションのイベントとウィンドウ状態をJSON Lines形式でログに追記する。
    /// </summary>
    void WriteEvent(
        const char* event,
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam,
        LRESULT result,
        const DisplayMode* display) noexcept
    {
        const DWORD previousError = GetLastError();
        AcquireSRWLockExclusive(&s_WriteLock);

        // Each process uses its working directory, including isolated fixtures.
        CreateDirectoryW(L"logs", nullptr);
        FILE* file = nullptr;
        if (_wfopen_s(&file, L"logs/application_lifecycle.jsonl", L"ab") == 0 && file != nullptr)
        {
            FILETIME wallTime{};
            GetSystemTimeAsFileTime(&wallTime);
            ULARGE_INTEGER wallTicks{};
            wallTicks.LowPart = wallTime.dwLowDateTime;
            wallTicks.HighPart = wallTime.dwHighDateTime;
            const unsigned long long unixMilliseconds =
                (wallTicks.QuadPart - 116444736000000000ull) / 10000ull;
            const bool valid = window != nullptr && IsWindow(window) != FALSE;
            const HWND foreground = GetForegroundWindow();
            DWORD windowProcess = 0;
            DWORD foregroundProcess = 0;
            const DWORD windowThread = valid
                ? GetWindowThreadProcessId(window, &windowProcess) : 0;
            const DWORD foregroundThread = foreground != nullptr
                ? GetWindowThreadProcessId(foreground, &foregroundProcess) : 0;
            RECT client{};
            const bool hasClientRectangle = valid && GetClientRect(window, &client) != FALSE;

            std::fputs("{\"event\":", file);
            WriteJsonString(file, event);
            std::fprintf(file,
                ",\"sequence\":%lld,\"pid\":%lu,\"tid\":%lu,"
                "\"unix_ms\":%llu,\"uptime_ms\":%llu,\"hwnd\":\"0x%llx\","
                "\"valid\":%s,\"visible\":%s,\"iconic\":%s,"
                "\"foreground\":\"0x%llx\",\"window_pid\":%lu,\"window_tid\":%lu,"
                "\"foreground_pid\":%lu,\"foreground_tid\":%lu,"
                "\"message\":%u,\"wparam\":%llu,\"lparam\":%lld,\"result\":%lld,"
                "\"hidden_window\":%s",
                static_cast<long long>(InterlockedIncrement64(&s_Sequence)),
                GetCurrentProcessId(), GetCurrentThreadId(), unixMilliseconds,
                static_cast<unsigned long long>(GetTickCount64()),
                static_cast<unsigned long long>(reinterpret_cast<UINT_PTR>(window)),
                valid ? "true" : "false",
                valid && IsWindowVisible(window) ? "true" : "false",
                valid && IsIconic(window) ? "true" : "false",
                static_cast<unsigned long long>(reinterpret_cast<UINT_PTR>(foreground)),
                windowProcess, windowThread, foregroundProcess, foregroundThread,
                message, static_cast<unsigned long long>(wParam),
                static_cast<long long>(lParam), static_cast<long long>(result),
                InterlockedCompareExchange(&s_HiddenWindow, 0, 0) != 0 ? "true" : "false");
            if (hasClientRectangle)
                std::fprintf(file, ",\"client_width\":%ld,\"client_height\":%ld",
                    client.right - client.left, client.bottom - client.top);
            if (display != nullptr)
                std::fprintf(file, ",\"width\":%u,\"height\":%u,\"fullscreen\":%s",
                    display->width, display->height, display->fullscreen ? "true" : "false");
            std::fputs("}\n", file);
            std::fclose(file);
        }

        ReleaseSRWLockExclusive(&s_WriteLock);
        SetLastError(previousError);
    }
}

/// <summary>
/// 非表示ウィンドウで動作するテストモードの状態を設定する。
/// </summary>
void ApplicationLifecycle::SetHiddenWindowMode(bool hidden) noexcept
{
    InterlockedExchange(&s_HiddenWindow, hidden ? 1 : 0);
}

/// <summary>
/// 指定されたイベントとウィンドウメッセージの引数および結果をログに記録する。
/// </summary>
void ApplicationLifecycle::Record(
    const char* event, HWND window, UINT message, WPARAM wParam,
    LPARAM lParam, LRESULT result) noexcept
{
    WriteEvent(event, window, message, wParam, lParam, result, nullptr);
}

/// <summary>
/// 指定されたイベントを画面寸法とフルスクリーン状態とともにログに記録する。
/// </summary>
void ApplicationLifecycle::RecordDisplayMode(
    const char* event, HWND window, UINT width, UINT height, bool fullscreen) noexcept
{
    const DisplayMode display{ width, height, fullscreen };
    WriteEvent(event, window, 0, 0, 0, 0, &display);
}

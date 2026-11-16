#pragma once

#include <Windows.h>

// Observes a few application events without participating in window/message
// handling. File failures are ignored and the caller's last-error value is kept.
namespace ApplicationLifecycle
{
    void SetHiddenWindowMode(bool hidden) noexcept;

    void Record(
        const char* event,
        HWND window,
        UINT message = 0,
        WPARAM wParam = 0,
        LPARAM lParam = 0,
        LRESULT result = 0) noexcept;

    void RecordDisplayMode(
        const char* event,
        HWND window,
        UINT width,
        UINT height,
        bool fullscreen) noexcept;
}

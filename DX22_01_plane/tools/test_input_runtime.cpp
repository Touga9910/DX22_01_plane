#include "input.h"
#include "Application.h"
#include "test_checks.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <string_view>

namespace
{
    bool poisonAllocation{};
    bool keyboardAvailable = true;
    bool controllerAvailable{};
    BYTE keyboard[256]{};
    XINPUT_STATE controller{};

    BOOL ReadKeyboard(PBYTE output)
    {
        if (!keyboardAvailable) return FALSE;
        std::memcpy(output, keyboard, sizeof(keyboard));
        return TRUE;
    }
    DWORD ReadController(DWORD, XINPUT_STATE* output)
    {
        if (!controllerAvailable) return ERROR_DEVICE_NOT_CONNECTED;
        *output = controller;
        return ERROR_SUCCESS;
    }
    DWORD WriteController(DWORD, XINPUT_VIBRATION*) { return ERROR_SUCCESS; }
    BOOL ReadCursor(POINT* output) { *output = {10, 20}; return TRUE; }
    BOOL CursorToClient(HWND, POINT*) { return TRUE; }
}

// Reproduce uninitialized heap state deterministically, without real input.
void* operator new(std::size_t size)
{
    void* result = std::malloc((std::max)(size, std::size_t{1}));
    if (!result) throw std::bad_alloc();
    if (poisonAllocation) std::memset(result, 0xcc, size);
    return result;
}
void operator delete(void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }

#define GetKeyboardState ReadKeyboard
#define XInputGetState ReadController
#define XInputSetState WriteController
#define GetCursorPos ReadCursor
#define ScreenToClient CursorToClient
#include "../input.cpp"
#undef GetKeyboardState
#undef XInputGetState
#undef XInputSetState
#undef GetCursorPos
#undef ScreenToClient

HWND Application::m_hWnd{};

int main(int argc, char** argv)
{
    const auto mode = argc > 1 ? std::string_view(argv[1]) : std::string_view{};
    poisonAllocation = true;
    Input::Create();
    poisonAllocation = false;
    if (mode.empty() || mode == "--startup")
    {
        assert(!Input::GetKeyPress(VK_A));
        assert(!Input::GetKeyTrigger(VK_A));
        assert(!Input::GetKeyRelease(VK_A));
        assert(!Input::GetButtonPress(XINPUT_A));
        assert(!Input::GetButtonTrigger(XINPUT_A));
        assert(Input::GetLeftAnalogStick().x == 0.0f);
        assert(Input::GetMouseMove().x == 0.0f);
    }
    keyboard[VK_A] = 0x80;
    controllerAvailable = true;
    controller.Gamepad.wButtons = XINPUT_A;
    controller.Gamepad.sThumbLX = 32767;
    controller.Gamepad.bLeftTrigger = 255;
    Input::Update();
    Input::Update();
    assert(Input::GetKeyPress(VK_A));
    assert(Input::GetButtonPress(XINPUT_A));
    if (mode.empty() || mode == "--disconnect")
    {
        controllerAvailable = false;
        Input::Update();
        assert(!Input::GetButtonPress(XINPUT_A));
        assert(!Input::GetButtonRelease(XINPUT_A));
        assert(Input::GetLeftAnalogStick().x == 0.0f);
        assert(Input::GetLeftTrigger() == 0.0f);
        Input::Update();
        assert(!Input::GetButtonRelease(XINPUT_A));
        controllerAvailable = true;
        Input::Update();
        assert(Input::GetButtonTrigger(XINPUT_A));
    }
    if (mode.empty() || mode == "--keyboard-failure")
    {
        keyboardAvailable = false;
        Input::Update();
        assert(!Input::GetKeyPress(VK_A));
        assert(!Input::GetKeyRelease(VK_A));
        Input::Update();
        assert(!Input::GetKeyRelease(VK_A));
        keyboardAvailable = true;
        Input::Update();
        assert(Input::GetKeyTrigger(VK_A));
        // A failed keyboard/mouse poll must not fire a charged shot by
        // manufacturing the left-button release used by PlayerBallAim.
        keyboard[VK_LBUTTON] = 0x80;
        Input::Update();
        assert(Input::GetKeyPress(VK_LBUTTON));
        keyboardAvailable = false;
        Input::Update();
        assert(!Input::GetKeyPress(VK_LBUTTON));
        assert(!Input::GetKeyRelease(VK_LBUTTON));
    }
    Input::Release();
    Input::Release();
    std::cout << "Input startup, failed reads, disconnect, and reconnect passed.\n";
}

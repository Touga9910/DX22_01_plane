#pragma once

#include    <Windows.h>
#include    <cstdint>

//-----------------------------------------------------------------------------
// Applicationクラス
//-----------------------------------------------------------------------------
class Application
{
public:
    Application(uint32_t width, uint32_t height);
    ~Application();
    void Run();

    /// <summary>
    /// アプリケーションに設定されたウィンドウの幅を返す。
    /// </summary>
    static uint32_t GetWidth() {
        return m_Width;
    }

    /// <summary>
    /// アプリケーションに設定されたウィンドウの高さを返す。
    /// </summary>
    static uint32_t GetHeight() {
        return m_Height;
    }

    /// <summary>
    /// アプリケーションが使用するウィンドウのハンドルを返す。
    /// </summary>
    static HWND GetWindow() {
        return m_hWnd;
    }

    static void SetDisplayMode(uint32_t width, uint32_t height, bool fullscreen);
    /// <summary>
    /// 現在フルスクリーン表示に設定されているかを返す。
    /// </summary>
    static bool IsFullscreen() { return m_IsFullscreen; }

private:
    static HINSTANCE   m_hInst;        // インスタンスハンドル
    static HWND        m_hWnd;         // ウィンドウハンドル
    static uint32_t    m_Width;        // ウィンドウの横幅
    static uint32_t    m_Height;       // ウィンドウの縦幅
    static uint32_t    m_WindowedWidth;
    static uint32_t    m_WindowedHeight;
    static bool        m_IsFullscreen;
    static bool        m_TestHiddenWindow;

    static bool InitApp(); //初期化
    static void UninitApp(); //終了処理
    static void MainLoop(); //メインループ

    //プロシージャ
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp);
};

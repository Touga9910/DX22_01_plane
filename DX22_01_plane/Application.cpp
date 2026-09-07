#include <chrono>
#include <thread>
#include <algorithm>
#include <stdio.h>
#include "Application.h"

#pragma execution_character_set("utf-8")
#include "Game.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"   // ← 追加
#include "imgui/imgui_impl_dx11.h"    // ← 追加
#include "Renderer.h"                  // ← 追加（Device取得のため）

// Win32 の ANSI/MBCS 設定に影響されないよう、ウィンドウ関連は Unicode API を使う。
constexpr wchar_t ClassName[] = L"DX22_01_plane_WindowClass"; // ウィンドウクラス名
constexpr wchar_t WindowName[] = L"2025 framework ひな型";    // ウィンドウ名

HINSTANCE  Application::m_hInst;   // インスタンスハンドル
HWND       Application::m_hWnd;    // ウィンドウハンドル
uint32_t   Application::m_Width;   // ウィンドウの横幅
uint32_t   Application::m_Height;  // ウィンドウの縦幅
uint32_t   Application::m_WindowedWidth;
uint32_t   Application::m_WindowedHeight;
bool       Application::m_IsFullscreen = false;

// ImGuiのWin32プロシージャハンドラ(マウス対応)
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

//-----------------------------------------------------------------------------
// コンストラクタ
//-----------------------------------------------------------------------------
Application::Application(uint32_t width, uint32_t height)
{ 
    m_Height = height;
    m_Width = width;
    m_WindowedWidth = width;
    m_WindowedHeight = height;

    timeBeginPeriod(1); //タイマー精度を1ミリ秒に設定
}

void Application::SetDisplayMode(
    uint32_t width,
    uint32_t height,
    bool fullscreen)
{
    if (m_hWnd == nullptr)
    {
        return;
    }

    m_WindowedWidth = (std::max)(640u, width);
    m_WindowedHeight = (std::max)(360u, height);
    m_IsFullscreen = fullscreen;

    if (fullscreen)
    {
        MONITORINFO monitorInfo{ sizeof(MONITORINFO) };
        GetMonitorInfoW(
            MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST),
            &monitorInfo);
        SetWindowLongPtrW(m_hWnd, GWL_STYLE, WS_POPUP | WS_MINIMIZEBOX);
        SetWindowPos(
            m_hWnd,
            HWND_TOP,
            monitorInfo.rcMonitor.left,
            monitorInfo.rcMonitor.top,
            monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
            monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
            SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        return;
    }

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
        WS_MINIMIZEBOX;
    RECT rectangle{
        0,
        0,
        static_cast<LONG>(m_WindowedWidth),
        static_cast<LONG>(m_WindowedHeight),
    };
    AdjustWindowRect(&rectangle, style, FALSE);
    SetWindowLongPtrW(m_hWnd, GWL_STYLE, style);
    SetWindowPos(
        m_hWnd,
        HWND_NOTOPMOST,
        100,
        100,
        rectangle.right - rectangle.left,
        rectangle.bottom - rectangle.top,
        SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

//-----------------------------------------------------------------------------
// デストラクタ
//-----------------------------------------------------------------------------
Application::~Application()
{ 
    timeEndPeriod(1); // タイマー精度を元に戻す
}

//-----------------------------------------------------------------------------
// 実行
//-----------------------------------------------------------------------------
void Application::Run()
{
    //初期化
    bool okfg = InitApp();
    if (okfg) { MainLoop(); }

    UninitApp(); // 終了処理
}

//-----------------------------------------------------------------------------
// 初期化処理
//-----------------------------------------------------------------------------
bool Application::InitApp()
{
    // インスタンスハンドルを取得
    auto hInst = GetModuleHandle(nullptr);
    if (hInst == nullptr)
    {
        return false;
    }

    // ウィンドウの設定
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hIcon = LoadIcon(hInst, IDI_APPLICATION);
    wc.hCursor = LoadCursor(hInst, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_BACKGROUND);
    wc.lpszMenuName = nullptr;
    wc.lpszClassName = ClassName;
    wc.hIconSm = LoadIcon(hInst, IDI_APPLICATION);

    // ウィンドウの登録
    if (!RegisterClassExW(&wc))
    {
        return false;
    }

    // インスタンスハンドル設定
    m_hInst = hInst;

    // ウィンドウのサイズを設定
    RECT rc = {};
    rc.right = static_cast<LONG>(m_Width);
    rc.bottom = static_cast<LONG>(m_Height);

    // ウィンドウサイズを調整
    auto style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    AdjustWindowRect(&rc, style, FALSE);

    // ウィンドウを生成
    m_hWnd = CreateWindowExW(
        0,
        //        WS_EX_TOPMOST,
        ClassName,
        WindowName,
        style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        rc.right - rc.left,
        rc.bottom - rc.top,
        nullptr,
        nullptr,
        m_hInst,
        nullptr);

    if (m_hWnd == nullptr)
    {
        return false;
    }

    // ウィンドウを表示
    ShowWindow(m_hWnd, SW_SHOWNORMAL);

    // ウィンドウを更新
    UpdateWindow(m_hWnd);

    // ウィンドウにフォーカスを設定
    SetFocus(m_hWnd);

    // 正常終了
    return true;

}

//-----------------------------------------------------------------------------
// 終了処理
//-----------------------------------------------------------------------------
void Application::UninitApp()
{
    // ウィンドウの登録を解除
    if (m_hInst != nullptr)
    {
        UnregisterClassW(ClassName, m_hInst);
    }

    m_hInst = nullptr;
    m_hWnd = nullptr;
}

//-----------------------------------------------------------------------------
// メインループ
//-----------------------------------------------------------------------------
void Application::MainLoop()
{
    MSG msg = {};

    // ゲーム初期化処理
    Game::Init();

    // ★ ImGui初期化
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    (void)io;

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    // プレイヤー向けImGui表示に必要な日本語グリフを読み込む。
    // 対応するWindows環境に同梱されるフォントを使い、
    // UIがデバッグ用のASCIIフォントへ依存しないようにする。
    ImFont* japaneseFont = io.Fonts->AddFontFromFileTTF(
        "C:/Windows/Fonts/meiryo.ttc",
        18.0f,
        nullptr,
        io.Fonts->GetGlyphRangesJapanese());
    if (japaneseFont != nullptr)
    {
        io.FontDefault = japaneseFont;
    }

    ImGui_ImplWin32_Init(m_hWnd);
    ImGui_ImplDX11_Init(Renderer::GetDevice(), Renderer::GetDeviceContext());
    ImGui::StyleColorsDark(); // テーマ（お好みで）
    
    // FPS計測用変数
   int fpsCounter = 0;
   int currentFps;
   long long oldTick = GetTickCount64(); // 前回計測時の時間
   long long nowTick = oldTick; // 今回計測時の時間

   // FPS固定用変数
   LARGE_INTEGER liWork; // workがつく変数は作業用変数
   long long frequency;// どれくらい細かく時間をカウントできるか
   QueryPerformanceFrequency(&liWork);
   frequency = liWork.QuadPart;
   // 時間（単位：カウント）取得
   QueryPerformanceCounter(&liWork);
   long long oldCount = liWork.QuadPart;// 前回計測時の時間
   long long nowCount = oldCount;// 今回計測時の時間

   int renderHz = 60;
#if defined(_DEBUG)
   // Only for isolated regression captures; production rendering remains 60 Hz.
   wchar_t testRenderHz[16] = {};
   if (GetEnvironmentVariableW(L"DX22_TEST_RENDER_HZ", testRenderHz, 16) > 0)
   {
       const int requestedHz = _wtoi(testRenderHz);
       if (requestedHz == 30 || requestedHz == 60 || requestedHz == 144)
           renderHz = requestedHz;
   }
#endif


   // ゲームループ
   while (1)
   {
       // 新たにメッセージがあれば
       if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
       {
           // ウィンドウプロシージャにメッセージを送る
           TranslateMessage(&msg);
           DispatchMessage(&msg);

           // 「WM_QUIT」メッセージを受け取ったらループを抜ける
           if (msg.message == WM_QUIT) {
               break;
           }
       }
        else
       {
           QueryPerformanceCounter(&liWork);// 現在時間を取得
           nowCount = liWork.QuadPart;
           // 1/60秒が経過したか？
           if (nowCount >= oldCount + frequency / renderHz) {

               // ★ ImGuiフレーム開始（Game::Draw()より前に必ず呼ぶ）
               ImGui_ImplDX11_NewFrame();
               ImGui_ImplWin32_NewFrame();
               ImGui::NewFrame();

               Game::Update(static_cast<double>(nowCount - oldCount) /
                   static_cast<double>(frequency));

               // ゲーム描画
               Game::Draw();

               
               // ★ ImGui描画（Game::Draw()より後に呼ぶ）
               ImGui::Render();
               ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

               ImGuiIO& io = ImGui::GetIO();
               if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
               {
                   ImGui::UpdatePlatformWindows();
                   ImGui::RenderPlatformWindowsDefault();
               }
               

               Renderer::DrawEnd();

               fpsCounter++; // ゲーム処理を実行したら＋１する
               oldCount = nowCount;
           }

           nowTick = GetTickCount64();

           // 1秒(1000ms)が経過したかチェック
           if (nowTick >= oldTick + 1000)
           {
               // FPSを計算し、変数に保存
               currentFps = fpsCounter;

               // タイトルバーに表示するための文字列を作成
               wchar_t titleBuffer[256];

               // 現在のウィンドウ名とFPSを組み合わせた文字列を作成
               swprintf_s(
                   titleBuffer,
                   256,
                   L"%ls [FPS: %d]",
                   WindowName, // 元のタイトル名
                   currentFps
               );

               // ウィンドウのタイトルバーを更新
               SetWindowTextW(m_hWnd, titleBuffer);

               // カウンタと時間をリセット
               fpsCounter = 0;
               oldTick = nowTick;
           }
        }
    }

   // ★ ImGui終了処理
   ImGui_ImplDX11_Shutdown();
   ImGui_ImplWin32_Shutdown();
   ImGui::DestroyContext();

   // ゲーム終了処理
   Game::Uninit();
}

//-----------------------------------------------------------------------------
// ウィンドウプロシージャ
//-----------------------------------------------------------------------------
LRESULT CALLBACK Application::WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
        return true;

    switch (uMsg)
    {
    case WM_DESTROY:// ウィンドウ破棄のメッセージ
        PostQuitMessage(0);// 「WM_QUIT」メッセージを送る　→　アプリ終了
        break;

    case WM_CLOSE:  // 「x」ボタンが押されたら
    {
        int res = MessageBoxW(NULL, L"終了しますか？", L"確認", MB_OKCANCEL);
        Game::ResetFrameTiming();
        if (res == IDOK) {
            DestroyWindow(hWnd);  // 「WM_DESTROY」メッセージを送る
        }
    }
    break;

    case WM_ACTIVATE:
        if (wParam == WA_INACTIVE) {
            // フルスクリーン表示かつメッセージボックス非表示なら
            if (m_IsFullscreen &&
                !(lParam != 0 && ImGui::GetCurrentContext() != nullptr &&
                  ImGui::FindViewportByPlatformHandle(reinterpret_cast<void*>(lParam)) != nullptr))
            {
                // 別のアプリへ切り替えた場合のみ、ウインドウを最小化する（タスク切替時に背後に残る問題対策）
                ShowWindow(hWnd, SW_MINIMIZE);
            }
        }
        // 標準挙動を実行
        return DefWindowProc(hWnd, uMsg, wParam, lParam);

    case WM_SIZE: //ウィンドウサイズに変更があったメッセージ

        Game::ResetFrameTiming();
        if (wParam != SIZE_MINIMIZED)
        {
            int width = LOWORD(lParam); //横幅
            int height = HIWORD(lParam); //縦幅
            Renderer::ResizeWindow(width, height);
        }
        break;

    case WM_EXITSIZEMOVE:
        Game::ResetFrameTiming();
        break;

    default:
        // 受け取ったメッセージに対してデフォルトの処理を実行
        return DefWindowProc(hWnd, uMsg, wParam, lParam);
        break;
    }

    return 0;
}


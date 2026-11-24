#include    "main.h"
#include    "Application.h"

/// <summary>
/// デバッグビルドではメモリリークの検査を設定し、アプリケーションを生成して実行する。
/// </summary>
int main(void)
{
#if defined(DEBUG) || defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif // デバッグビルド用の設定

	// アプリケーション実行
	Application app(SCREEN_WIDTH, SCREEN_HEIGHT);
	app.Run();

	return 0;
}
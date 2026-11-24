#pragma once
#include "Scene.h"
#include "MenuSelection.h"
#include "TitleSequence.h"
#include <chrono>
#include <string>
#include <vector>

class GameObject;
class TitleTable;

// TitleSceneクラス
class TitleScene : public Scene
{
private:
	std::vector<GameObject*> m_SceneGameObjects;

	MenuSelection m_Menu;
	TitleTable* m_Table=nullptr;
	TitleSequence m_Sequence;
	std::chrono::steady_clock::time_point m_LastTick=std::chrono::steady_clock::now();
	float m_HoverFlash=0, m_QuitFade=-1;
	bool m_ShowQuit=false, m_Logo=false;
	bool m_CanContinue = false;
	bool m_ConfirmNewRun = false;
	bool m_ShowProgression = false;
	std::string m_SaveSummary;
	std::string m_Message;

	void Init(); // 初期化
	void Uninit(); // 終了処理

public:
	TitleScene(); // コンストラクタ
	~TitleScene(); // デストラクタ

    /// <summary>
    /// タイトル演出や確認画面の状態から、一時停止を許可できるかを返す。
    /// </summary>
    bool AllowsPause() const { return !m_Sequence.Active() && m_QuitFade < 0 && !m_ShowQuit && !m_ShowProgression; }
    /// <summary>
    /// 開始演出のフェードが完了して開始処理が確定したかを返す。
    /// </summary>
    bool IsStartFadeComplete() const { return m_Sequence.phase == TitleSequence::Phase::Committed; }
	void Update(); // 更新
	void DrawUI() override;
	void DrawProgressionUI();
};


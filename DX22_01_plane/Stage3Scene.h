#pragma once
#include "Scene.h"
#include "Object.h"

// Stage3Sceneクラス
class Stage3Scene : public Scene
{
private:
	std::vector<Object*> m_MySceneObjects; // このシーンのオブジェクト

	int m_State = 0;//状態 0:ボール移動中,1:方向選択中,2:パワー選択中
	int m_Par = 0;	//パー（標準打数）
	int m_StrokeCount = 0;//現在の打数

	void Init(); // 初期化
	void Uninit(); // 終了処理

public:
	Stage3Scene(); // コンストラクタ
	~Stage3Scene(); // デストラクタ

	void Update(); // 更新

	int GetScore() const;//スコア取得
};


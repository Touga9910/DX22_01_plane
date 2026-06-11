#pragma once
#include <iostream>

//オブジェクト情報のあるファイルをインクルード
//#include "TestPlane.h"
//#include "TestCube.h"
//#include "TestGolfFlag.h"
//#include "TestModel.h"
//#include "
// .h"
//#include "Ground.h"


#include"Renderer.h"
#include"TitleScene.h"
#include"Stage1Scene.h"
#include"Stage2Scene.h"
#include"Stage3Scene.h"
#include"ResultScene.h"
#include"StageSelectScene.h"

#include"SkyBox.h"

#include "input.h"

enum SceneName {
	TITLE,
	SELECT,
	STAGE1,
	STAGE2,
	STAGE3,
	RESULT,
	SCENE_MAX
};

class Game
{
private:
	static Game* m_Instance;//ゲームインスタンス

	Scene* m_Scene;//シーン

	// カメラ
	Camera&  m_Camera = Camera::GetInstance();

	// スカイボックス
	SkyBox* m_SkyBox = nullptr;

	//オブジェクト配列
	std::vector<std::unique_ptr<Object>> m_Objects;

public:
	Game(); // コンストラクタ
	~Game(); // デストラクタ

	static void Init(); // 初期化
	static void Update(); // 更新
	static void Draw(); // 描画
	static void Uninit(); // 終了処理

	static Game* GetInstance();

	void ChangeScene(SceneName sName);	//シーンを変更
	void DeleteObject(Object* pt);		//オブジェクトを削除する
	void DeleteAllObject();				//オブジェクトを全て削除する

	static Camera* GetCamera() { return &m_Instance->m_Camera; }
	static SkyBox* GetSkyBox();

	//オブジェクトを追加する（※テンプレート関数）
	template<typename T> T* AddObject()
	{
		T* pt = new T;
		m_Instance->m_Objects.emplace_back(pt);
		pt->Init();
		return pt;
	}

	//オブジェクトを取得する
	template<typename T>std::vector<T*> GetObjects()
	{
		std::vector<T*>res; 
		for (auto& o : m_Instance->m_Objects)
		{
			//dynamic_castで型をチェック
			if (T* derivedObj = dynamic_cast<T*>(o.get()))
			{
				res.emplace_back(derivedObj);
			}
		}
		return res;
	}
};

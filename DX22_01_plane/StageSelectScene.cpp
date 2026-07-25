#include "StageSelectScene.h"
#include "Game.h"
#include "Input.h"
#include "Texture2D.h"
#include "imgui/imgui.h"

namespace
{
	constexpr const char* kNodeNames[] =
	{
		"Battle",
		"Rest Site",
		"Shop",
	};

	constexpr int kNodeCount = static_cast<int>(sizeof(kNodeNames) / sizeof(kNodeNames[0]));

}

// コンストラクタ
StageSelectScene::StageSelectScene()
{
	Init();
}

// デストラクタ
StageSelectScene::~StageSelectScene()
{
	Uninit();
}

// 初期化
void StageSelectScene::Init()
{

	m_SelectedNode = 0;

	//背景画像オブジェクトを作成
	Texture2D* pt = Game::GetInstance()->AddObject<Texture2D>();
	pt->SetTexture("assets/texture/background1.png");
	pt->SetPosition(0.0f, 0.0f, 0.0f);
	pt->SetRotation(0.0f, 0.0f, 0.0f);
	pt->SetScale(1280.0f, 720.0f, 0.0f);
	m_MySceneObjects.emplace_back(pt);

}

// 更新
void StageSelectScene::Update()
{
	if (Input::GetKeyTrigger(VK_S) || Input::GetKeyTrigger(VK_DOWN))
	{
		m_SelectedNode = (m_SelectedNode + 1) % kNodeCount;
	}
	if (Input::GetKeyTrigger(VK_W) || Input::GetKeyTrigger(VK_UP))
	{
		m_SelectedNode = (m_SelectedNode + kNodeCount - 1) % kNodeCount;
	}

	if (Input::GetKeyTrigger(VK_RETURN) || Input::GetKeyTrigger(VK_SPACE))
	{
		Game* game = Game::GetInstance();
		switch (m_SelectedNode)
		{
		case 0:
			game->StartNextBattle();
			break;
		case 1:
			game->ChangeScene(SceneType::RestSite);
			break;
		case 2:
			game->ChangeScene(SceneType::Shop);
			break;
		default:
			break;
		}
	}
}

void StageSelectScene::DrawUI()
{
	ImGui::SetNextWindowPos(ImVec2(340.0f, 80.0f), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(600.0f, 560.0f), ImGuiCond_Always);
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;

	ImGui::Begin("Route Select", nullptr, flags);
	ImGui::Text("Floor %d", Game::GetInstance()->GetClearedStageCount() + 1);
	ImGui::Text("HP %d / %d    Money %d    Deck %d",
		Game::GetInstance()->GetPlayerCurrentHp(),
		Game::GetInstance()->GetPlayerMaxHp(),
		Game::GetInstance()->GetPlayerMoney(),
		Game::GetInstance()->GetDeckBallCount());
	ImGui::Separator();
	ImGui::TextUnformatted("Choose the next node");

	for (int index = 0; index < kNodeCount; index++)
	{
		ImGui::Text("%s %s", index == m_SelectedNode ? ">" : " ", kNodeNames[index]);
	}
	ImGui::TextUnformatted("Next battle stage: Random");

	ImGui::Separator();
	ImGui::TextUnformatted("W/S or UP/DOWN : Select");
	ImGui::TextUnformatted("ENTER or SPACE : Enter node");
	ImGui::End();
}

// 終了処理
void StageSelectScene::Uninit()
{
	// このシーンのオブジェクトを削除する
	for (auto& o : m_MySceneObjects) {
		Game::GetInstance()->DeleteComponent(o);
	}
	m_MySceneObjects.clear();
}

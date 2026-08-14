#include "StageSelectScene.h"
#include "Game.h"
#include "Input.h"
#include "Texture2D.h"
#include "Texture2DFactory.h"
#include "imgui/imgui.h"

namespace
{
	// 各枠の抽選比率。値を変更するだけで出現確率を調整できる。
	constexpr int kBattleWeight = 6;
	constexpr int kShopWeight = 2;
	constexpr int kRestSiteWeight = 2;

	static_assert(
		kBattleWeight + kShopWeight + kRestSiteWeight > 0,
		"At least one route weight must be greater than zero.");
	static_assert(
		kBattleWeight >= 0 && kShopWeight >= 0 && kRestSiteWeight >= 0,
		"Route weights must not be negative.");

	const char* GetRouteName(StageRouteType routeType)
	{
		switch (routeType)
		{
		case StageRouteType::Battle:
			return "Battle";
		case StageRouteType::Shop:
			return "Shop";
		case StageRouteType::RestSite:
			return "Rest Site";
		default:
			return "Unknown";
		}
	}

	const char* GetRouteId(StageRouteType routeType)
	{
		switch (routeType)
		{
		case StageRouteType::Battle:
			return "battle";
		case StageRouteType::Shop:
			return "shop";
		case StageRouteType::RestSite:
			return "rest";
		default:
			return "unknown";
		}
	}
}

// コンストラクタ
StageSelectScene::StageSelectScene()
	: m_RandomEngine(Game::GetInstance()->GetNextRouteRandomSeed())
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
	RollRouteNodes();

	//背景画像オブジェクトを作成
	Texture2D* pt = Texture2DFactory::Create(*Game::GetInstance());
	pt->SetTexture("assets/texture/background1.png");
	pt->SetPosition(0.0f, 0.0f, 0.0f);
	pt->SetRotation(0.0f, 0.0f, 0.0f);
	pt->SetScale(1280.0f, 720.0f, 0.0f);
	m_SceneGameObjects.emplace_back(pt->GetGameObject());

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
		ChooseRoute(m_SelectedNode, "human");
	}
}

void StageSelectScene::RollRouteNodes()
{
	const std::array<int, 3> weights =
	{
		kBattleWeight,
		kShopWeight,
		kRestSiteWeight
	};
	std::discrete_distribution<int> routeDistribution(
		weights.begin(),
		weights.end());

	for (StageRouteType& routeType : m_RouteNodes)
	{
		routeType =
			static_cast<StageRouteType>(routeDistribution(m_RandomEngine));
	}
}

int StageSelectScene::GetRouteNodeCount() const
{
	return static_cast<int>(m_RouteNodes.size());
}

const char* StageSelectScene::GetRouteIdAt(int routeIndex) const
{
	if (routeIndex < 0 || routeIndex >= GetRouteNodeCount())
	{
		return "unknown";
	}
	return GetRouteId(m_RouteNodes[routeIndex]);
}

const char* StageSelectScene::GetRouteDisplayNameAt(int routeIndex) const
{
	if (routeIndex < 0 || routeIndex >= GetRouteNodeCount())
	{
		return "Unknown";
	}
	return GetRouteName(m_RouteNodes[routeIndex]);
}

bool StageSelectScene::ChooseRoute(
	int routeIndex,
	const std::string& controllerType)
{
	if (routeIndex < 0 || routeIndex >= GetRouteNodeCount())
	{
		return false;
	}

	const StageRouteType routeType = m_RouteNodes[routeIndex];
	Game* game = Game::GetInstance();
	nlohmann::json offeredRoutes = nlohmann::json::array();
	for (const StageRouteType offeredRoute : m_RouteNodes)
	{
		offeredRoutes.push_back(GetRouteName(offeredRoute));
	}
	game->RecordBalanceEvent(
		"route_choice",
		{
			{ "controller", controllerType },
			{ "offered_routes", std::move(offeredRoutes) },
			{ "selected_index", routeIndex },
			{ "selected_route", GetRouteName(routeType) },
		});
	switch (routeType)
	{
	case StageRouteType::Battle:
		game->StartNextBattle();
		break;
	case StageRouteType::Shop:
		game->ChangeScene(SceneType::Shop);
		break;
	case StageRouteType::RestSite:
		game->ChangeScene(SceneType::RestSite);
		break;
	default:
		return false;
	}
	return true;
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
		ImGui::Text(
			"%s %s",
			index == m_SelectedNode ? ">" : " ",
			GetRouteName(m_RouteNodes[index]));
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
	for (GameObject* gameObject : m_SceneGameObjects) {
		Game::GetInstance()->DeleteGameObject(gameObject);
	}
	m_SceneGameObjects.clear();
}

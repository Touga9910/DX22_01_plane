#include "StageSelectScene.h"
#include "Game.h"
#include "Input.h"
#include "Texture2D.h"
#include "Texture2DFactory.h"
#include "UiText.h"
#include "imgui/imgui.h"

namespace
{
	// 各枠の抽選比率。値を変更するだけで出現確率を調整できる。
	// Facilities are 12% each. The remaining 76% keeps the existing
	// normal-battle-to-midboss ratio of 5:1.
	constexpr int kNormalBattleWeight = 95;
	constexpr int kMidBossWeight = 19;
	constexpr int kShopWeight = 18;
	constexpr int kRestSiteWeight = 18;

	static_assert(
		kNormalBattleWeight + kMidBossWeight +
		kShopWeight + kRestSiteWeight > 0,
		"At least one route weight must be greater than zero.");
	static_assert(
		kNormalBattleWeight >= 0 && kMidBossWeight >= 0 &&
		kShopWeight >= 0 && kRestSiteWeight >= 0,
		"Route weights must not be negative.");

	const char* GetRouteName(StageRouteType routeType)
	{
		switch (routeType)
		{
		case StageRouteType::NormalBattle: return "Normal Battle";
		case StageRouteType::MidBoss: return "Mid Boss";
		case StageRouteType::Shop:
			return "Shop";
		case StageRouteType::RestSite:
			return "Rest Site";
		case StageRouteType::FinalBoss: return "Final Boss";
		default:
			return "Unknown";
		}
	}

	const char* GetLocalizedRouteName(StageRouteType routeType)
	{
		switch (routeType)
		{
		case StageRouteType::NormalBattle: return UiText::RouteBattle;
		case StageRouteType::MidBoss:
			return RelicUtf8(u8"\u4e2d\u30dc\u30b9");
		case StageRouteType::Shop: return UiText::RouteShop;
		case StageRouteType::RestSite: return UiText::RouteRest;
		case StageRouteType::FinalBoss:
			return RelicUtf8(u8"\u6700\u7d42\u30dc\u30b9");
		default: return "Unknown";
		}
	}

	const char* GetRouteId(StageRouteType routeType)
	{
		switch (routeType)
		{
		case StageRouteType::NormalBattle: return "battle";
		case StageRouteType::MidBoss: return "midboss";
		case StageRouteType::Shop:
			return "shop";
		case StageRouteType::RestSite:
			return "rest";
		case StageRouteType::FinalBoss: return "final_boss";
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
	const int nodeCount = GetRouteNodeCount();
	if (Input::GetKeyTrigger(VK_S) || Input::GetKeyTrigger(VK_DOWN))
	{
		m_SelectedNode = (m_SelectedNode + 1) % nodeCount;
	}
	if (Input::GetKeyTrigger(VK_W) || Input::GetKeyTrigger(VK_UP))
	{
		m_SelectedNode = (m_SelectedNode + nodeCount - 1) % nodeCount;
	}

	if (Input::GetKeyTrigger(VK_RETURN) || Input::GetKeyTrigger(VK_SPACE))
	{
		ChooseRoute(m_SelectedNode, "human");
	}
}

void StageSelectScene::RollRouteNodes()
{
	if (Game::GetInstance()->IsFinalBossRoute())
	{
		m_RouteNodes.fill(StageRouteType::FinalBoss);
		return;
	}

	const std::array<int, 4> weights =
	{
		kNormalBattleWeight,
		kMidBossWeight,
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
	return Game::GetInstance()->IsFinalBossRoute()
		? 1
		: static_cast<int>(m_RouteNodes.size());
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
			{ "area_progress", game->GetAreaProgress() },
			{ "run_phase", ToString(game->GetRunPhase()) },
		});
	switch (routeType)
	{
	case StageRouteType::NormalBattle:
		game->StartNextBattle(StageType::Normal);
		break;
	case StageRouteType::MidBoss:
		game->StartNextBattle(StageType::MidBoss);
		break;
	case StageRouteType::Shop:
		game->ChangeScene(SceneType::Shop);
		break;
	case StageRouteType::RestSite:
		game->ChangeScene(SceneType::RestSite);
		break;
	case StageRouteType::FinalBoss:
		game->StartNextBattle(StageType::Boss);
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

	ImGui::Begin(UiText::RouteWindow, nullptr, flags);
	ImGui::Text(
		RelicUtf8(u8"\u901a\u5e38\u30a8\u30ea\u30a2 %d / %d"),
		Game::GetInstance()->GetAreaProgress(),
		Game::GetInstance()->GetNormalRouteAreaGoal());
	ImGui::Text(
		RelicUtf8(u8"\u30d5\u30a7\u30fc\u30ba: %s"),
		ToString(Game::GetInstance()->GetRunPhase()));
	ImGui::Text(UiText::RunStatusFormat,
		Game::GetInstance()->GetPlayerCurrentHp(),
		Game::GetInstance()->GetPlayerMaxHp(),
		Game::GetInstance()->GetPlayerMoney(),
		Game::GetInstance()->GetDeckBallCount());
	ImGui::Separator();
	ImGui::TextUnformatted(UiText::ChooseNode);

	for (int index = 0; index < GetRouteNodeCount(); index++)
	{
		ImGui::Text(
			"%s %s",
			index == m_SelectedNode ? ">" : " ",
			GetLocalizedRouteName(m_RouteNodes[index]));
	}
	ImGui::TextUnformatted(
		Game::GetInstance()->IsFinalBossRoute()
		? RelicUtf8(u8"\u3053\u306e\u5148\u306f\u6700\u7d42\u30dc\u30b9\u3067\u3059\u3002")
		: UiText::NextBattleRandom);

	ImGui::Separator();
	ImGui::TextUnformatted(UiText::RouteSelectControls);
	ImGui::TextUnformatted(UiText::RouteEnterControls);
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

#pragma execution_character_set("utf-8")
#include "StageSelectScene.h"
#include "Game.h"
#include "GameUi.h"
#include "Input.h"
#include "Texture2D.h"
#include "Texture2DFactory.h"
#include "UiText.h"
#include "imgui/imgui.h"

namespace
{
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
		case StageRouteType::BossPreparation: return "Boss Preparation";
		default:
			return "Unknown";
		}
	}

	const char* GetLocalizedRouteName(StageRouteType routeType)
	{
		switch (routeType)
		{
		case StageRouteType::NormalBattle: return "通常戦闘";
		case StageRouteType::BossPreparation: return "ボス前休憩";
		case StageRouteType::MidBoss:
			return RelicUtf8(u8"\u4e2d\u30dc\u30b9");
		case StageRouteType::Shop: return UiText::RouteShop;
		case StageRouteType::RestSite: return UiText::RouteRest;
		case StageRouteType::FinalBoss:
			return RelicUtf8(u8"\u6700\u7d42\u30dc\u30b9");
		default: return "Unknown";
		}
	}

    ImU32 RouteColor(StageRouteType type)
    {
        switch (type)
        {
        case StageRouteType::MidBoss: return IM_COL32(229, 114, 105, 255);
        case StageRouteType::Shop: return IM_COL32(232, 190, 91, 255);
        case StageRouteType::RestSite:
        case StageRouteType::BossPreparation: return IM_COL32(108, 207, 162, 255);
        case StageRouteType::FinalBoss: return IM_COL32(204, 143, 230, 255);
        default: return IM_COL32(133, 180, 219, 255);
        }
    }
    const char* RouteHint(StageRouteType type)
    {
        switch (type)
        {
        case StageRouteType::MidBoss: return "強敵との戦闘。撃破するとレリックを選べます。";
        case StageRouteType::Shop: return "Moneyでボールやレリックを購入し、デッキを調整できます。";
        case StageRouteType::RestSite: return "HP回復かボール強化を選べます。";
        case StageRouteType::BossPreparation: return "15エリアの後に必ず立ち寄る休憩所です。";
        case StageRouteType::FinalBoss: return "最終戦。撃破すればランをクリアします。";
        default: return "通常の戦闘。Moneyとクリア報酬を獲得できます。";
        }
    }

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
	m_RouteNodes = Game::GetInstance()->GetRunMap().Available();

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
	const bool mouseConfirmed = m_MouseConfirmed;
	m_MouseConfirmed = false;
	if (nodeCount == 0) return;
	if (Input::GetKeyTrigger(VK_S) || Input::GetKeyTrigger(VK_DOWN))
	{
		m_SelectedNode = (m_SelectedNode + 1) % nodeCount;
	}
	if (Input::GetKeyTrigger(VK_W) || Input::GetKeyTrigger(VK_UP))
	{
		m_SelectedNode = (m_SelectedNode + nodeCount - 1) % nodeCount;
	}

	if (mouseConfirmed || Input::GetKeyTrigger(VK_RETURN) || Input::GetKeyTrigger(VK_SPACE))
	{
		ChooseRoute(m_SelectedNode, "human");
	}
}

int StageSelectScene::GetRouteNodeCount() const
{
    return static_cast<int>(m_RouteNodes.size());
}

int StageSelectScene::GetMapNodeIdAt(int routeIndex) const
{
    return routeIndex >= 0 && routeIndex < GetRouteNodeCount() ? m_RouteNodes[routeIndex] : -1;
}

const char* StageSelectScene::GetRouteIdAt(int routeIndex) const
{
	if (routeIndex < 0 || routeIndex >= GetRouteNodeCount())
	{
		return "unknown";
	}
	return RunMapRouteId(Game::GetInstance()->GetRunMap().Node(m_RouteNodes[routeIndex])->type);
}

const char* StageSelectScene::GetRouteDisplayNameAt(int routeIndex) const
{
	if (routeIndex < 0 || routeIndex >= GetRouteNodeCount())
	{
		return "Unknown";
	}
	return GetRouteName(Game::GetInstance()->GetRunMap().Node(m_RouteNodes[routeIndex])->type);
}

bool StageSelectScene::ChooseRoute(
	int routeIndex,
	const std::string& controllerType)
{
	if (routeIndex < 0 || routeIndex >= GetRouteNodeCount())
	{
		return false;
	}

	const int nodeId = m_RouteNodes[routeIndex];
	const StageRouteType routeType = Game::GetInstance()->GetRunMap().Node(nodeId)->type;
	Game* game = Game::GetInstance();
	nlohmann::json offeredRoutes = nlohmann::json::array();
	for (const int offeredRoute : m_RouteNodes)
	{
		offeredRoutes.push_back(GetRouteName(game->GetRunMap().Node(offeredRoute)->type));
	}
	if (!game->ChooseMapNode(nodeId)) return false;
	game->RecordBalanceEvent(
		"route_choice",
		{
			{ "controller", controllerType },
			{ "offered_routes", std::move(offeredRoutes) },
			{ "selected_index", routeIndex },
			{ "map_node_id", nodeId },
			{ "map_path", game->GetRunMap().Path() },
			{ "map_version", 1 },
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
	return game->GetRunMap().Active() == nodeId;
}

void StageSelectScene::DrawUI()
{
    GameUi::PrepareWindow("run_map", ImVec2(100, 30), ImVec2(1080, 660));
    ImGui::SetNextWindowBgAlpha(0.98f);
    if (!ImGui::Begin("ルートマップ", nullptr, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }
    Game* game = Game::GetInstance();
    const RunMap& map = game->GetRunMap();
    ImGui::Text("通常エリア %d / %d", game->GetAreaProgress(), game->GetNormalRouteAreaGoal());
    ImGui::SameLine(260);
    ImGui::Text(UiText::RunStatusFormat, game->GetPlayerCurrentHp(), game->GetPlayerMaxHp(), game->GetPlayerMoney(), game->GetDeckBallCount());
    ImGui::TextUnformatted("下から上へ進みます。光っている行き先をクリックして出発。先の道はスクロールで確認できます。");
    if (ImGui::Button("現在地へ")) m_FocusCurrent = true;
    ImGui::SameLine();
    if (ImGui::Checkbox("全体表示", &m_ShowWholeMap)) m_FocusCurrent = true;
    ImGui::Separator();

    const float sideWidth = 230.0f;
    const float mapWidth = (std::max)(360.0f, ImGui::GetContentRegionAvail().x - sideWidth - 16.0f);
    ImGui::BeginChild("map_canvas", ImVec2(mapWidth, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    const float viewHeight = ImGui::GetContentRegionAvail().y;
    const int rows = map.AreaCount() + 2;
    const float rowHeight = m_ShowWholeMap ? (std::max)(26.0f, (viewHeight - 45.0f) / rows) : 88.0f;
    const float canvasWidth = (std::max)(430.0f, ImGui::GetContentRegionAvail().x);
    const float nodeWidth = m_ShowWholeMap ? 86.0f : 116.0f;
    const float nodeHeight = m_ShowWholeMap ? 22.0f : 38.0f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 localOrigin = ImGui::GetCursorPos();
    auto position = [&](const RunMapNode& n)
    {
        return ImVec2(origin.x + 85.0f + (canvasWidth - 170.0f) * (n.lane / 2.0f),
            origin.y + 24.0f + (rows - 1 - (n.floor - map.StartArea())) * rowHeight);
    };
    auto* draw = ImGui::GetWindowDrawList();
    // Edges go behind the nodes. Gold records the chosen path; gray is unavailable.
    for (const auto& node : map.Nodes())
    {
        for (int nextId : node.next)
        {
            const auto& next = *map.Node(nextId);
            const bool used = map.Visited(node.id) && map.Visited(nextId);
            const bool reachable = (map.Reachable(node.id) || (!map.Path().empty() && map.Path().back() == node.id)) && map.Reachable(nextId);
            const ImU32 color = used ? IM_COL32(243, 207, 118, 255) :
                reachable ? IM_COL32(117, 142, 161, 230) : IM_COL32(60, 65, 73, 150);
            ImVec2 from = position(node), to = position(next);
            from.y -= nodeHeight * 0.5f; to.y += nodeHeight * 0.5f;
            draw->AddLine(from, to, color, used ? 3.0f : 1.5f);
        }
    }
    for (const auto& node : map.Nodes())
    {
        const ImVec2 center = position(node);
        const ImVec2 lo(center.x - nodeWidth * 0.5f, center.y - nodeHeight * 0.5f);
        const ImVec2 hi(center.x + nodeWidth * 0.5f, center.y + nodeHeight * 0.5f);
        const auto found = std::find(m_RouteNodes.begin(), m_RouteNodes.end(), node.id);
        const bool selectable = found != m_RouteNodes.end();
        const bool visited = map.Visited(node.id);
        const bool reachable = map.Reachable(node.id);
        const bool current = !map.Path().empty() && map.Path().back() == node.id;
        ImU32 border = visited ? IM_COL32(243, 207, 118, 255) : RouteColor(node.type);
        if (!visited && !reachable) border = IM_COL32(83, 88, 98, 180);
        draw->AddRectFilled(lo, hi, selectable ? IM_COL32(43, 62, 77, 255) : IM_COL32(25, 31, 40, 255), 7.0f);
        draw->AddRect(lo, hi, border, 7.0f, 0, selectable || current ? 3.0f : 1.0f);
        const char* label = GetLocalizedRouteName(node.type);
        const float fontSize = m_ShowWholeMap ? 13.0f : ImGui::GetFontSize();
        const ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(fontSize, 1000.0f, 0.0f, label);
        draw->AddText(ImGui::GetFont(), fontSize, ImVec2(center.x - textSize.x / 2, center.y - textSize.y / 2), border, label);
        if (node.lane == 0)
        {
            char area[16]; sprintf_s(area, "%02d", node.floor + 1);
            draw->AddText(ImVec2(origin.x + 3, center.y - 8), IM_COL32(153, 163, 176, 255), area);
        }
        if (current) draw->AddCircleFilled(ImVec2(hi.x + 7, center.y), 3.5f, IM_COL32(243, 207, 118, 255));
        ImGui::SetCursorScreenPos(lo);
        ImGui::PushID(node.id);
        if (ImGui::InvisibleButton("node", ImVec2(nodeWidth, nodeHeight)) && selectable)
        {
            m_SelectedNode = static_cast<int>(found - m_RouteNodes.begin());
            m_MouseConfirmed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(label);
            ImGui::TextUnformatted(RouteHint(node.type));
            ImGui::TextUnformatted(selectable ? "クリックしてこの道へ進む" :
                current ? "現在地" : visited ? "通過済み" : reachable ? "先の行き先" : "現在の経路からは進めません");
            ImGui::EndTooltip();
        }
        ImGui::PopID();
    }
    ImGui::SetCursorPos(localOrigin);
    ImGui::Dummy(ImVec2(canvasWidth, rows * rowHeight + 45.0f));
    if (m_FocusCurrent && !m_RouteNodes.empty())
    {
        const float nextY = position(*map.Node(m_RouteNodes.front())).y - origin.y;
        ImGui::SetScrollY(m_ShowWholeMap ? 0.0f : (std::max)(0.0f, nextY - viewHeight * 0.72f));
        m_FocusCurrent = false;
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("map_details", ImVec2(0, 0));
    ImGui::TextUnformatted("次の行き先");
    for (int index = 0; index < GetRouteNodeCount(); ++index)
    {
        const auto& node = *map.Node(m_RouteNodes[index]);
        const char* lane = node.lane == 0 ? "左" : node.lane == 1 ? "中央" : "右";
        ImGui::PushID(index);
        ImGui::Text("%sの道", lane);
        if (ImGui::Button(GetLocalizedRouteName(node.type), ImVec2(-1, 38)))
        {
            m_SelectedNode = index; m_MouseConfirmed = true;
        }
        ImGui::TextWrapped("%s", RouteHint(node.type));
        ImGui::Spacing();
        ImGui::PopID();
    }
    ImGui::Separator();
    ImGui::TextWrapped("金色の線：通った道\n明るい線：これから進める道\n暗いノード：選べなくなった道");
    ImGui::Spacing();
    ImGui::TextWrapped("15エリア → ボス前休憩 → 最終ボス");
    if (map.StartArea() > 0) ImGui::TextWrapped("このマップはエリア%dからの経路です。", map.StartArea() + 1);
    ImGui::EndChild();
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

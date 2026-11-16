#pragma execution_character_set("utf-8")
#include "StageSelectScene.h"
#include "GameRuntime.h"
#include "IllustratedUi.h"
#include "Input.h"
#include "RunRouteCommands.h"
#include "RunRouteView.h"
#include "UiText.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <cstdio>

namespace
{
	const char* UiUtf8(const char8_t* text) noexcept
	{
		return reinterpret_cast<const char*>(text);
	}

	const char* GetLocalizedRouteName(StageRouteType routeType)
	{
		switch (routeType)
		{
		case StageRouteType::NormalBattle: return "通常戦闘";
		case StageRouteType::BossPreparation: return "ボス前休憩";
		case StageRouteType::MidBoss:
			return UiUtf8(u8"\u4e2d\u30dc\u30b9");
		case StageRouteType::Shop: return UiText::RouteShop;
		case StageRouteType::RestSite: return UiText::RouteRest;
		case StageRouteType::FinalBoss:
			return UiUtf8(u8"\u6700\u7d42\u30dc\u30b9");
		default: return "Unknown";
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

    const char* RouteArt(StageRouteType type)
    {
        switch (type)
        {
        case StageRouteType::MidBoss:
        case StageRouteType::FinalBoss: return "assets/UI/Run/node_midboss.png";
        case StageRouteType::Shop: return "assets/UI/Run/node_shop.png";
        case StageRouteType::RestSite:
        case StageRouteType::BossPreparation: return "assets/UI/Run/node_rest.png";
        default: return "assets/UI/Run/node_battle.png";
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
	m_RouteNodes = RunRouteView::AvailableNodeIds();

}

// 更新
void StageSelectScene::Update()
{
	const int nodeCount = GetRouteNodeCount();
	const bool mouseConfirmed = m_MouseConfirmed;
	m_MouseConfirmed = false;
	if (nodeCount == 0) return;
	if (Input::GetKeyTrigger(VK_S) || Input::GetKeyTrigger(VK_DOWN) ||
        Input::GetKeyTrigger(VK_D) || Input::GetKeyTrigger(VK_RIGHT))
	{
		m_SelectedNode = (m_SelectedNode + 1) % nodeCount;
	}
	if (Input::GetKeyTrigger(VK_W) || Input::GetKeyTrigger(VK_UP) ||
        Input::GetKeyTrigger(VK_A) || Input::GetKeyTrigger(VK_LEFT))
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
	return RunMapRouteId(RunRouteView::NodeType(m_RouteNodes[routeIndex]));
}

const char* StageSelectScene::GetRouteDisplayNameAt(int routeIndex) const
{
	if (routeIndex < 0 || routeIndex >= GetRouteNodeCount())
	{
		return "Unknown";
	}
	return RunRouteView::LogName(RunRouteView::NodeType(m_RouteNodes[routeIndex]));
}

bool StageSelectScene::ChooseRoute(
	int routeIndex,
	const std::string& controllerType)
{
	if (routeIndex < 0 || routeIndex >= GetRouteNodeCount())
	{
		return false;
	}

	return RunRouteCommands::Choose(
		m_RouteNodes[routeIndex],
		routeIndex,
		m_RouteNodes,
		controllerType);
}

void StageSelectScene::DrawUI()
{
    using namespace IllustratedUi;
    Canvas ui("run_map_art", 1672, 941);
    ui.Art("assets/UI/Run/map_background.png", 0, 0, 1672, 941);
    ui.Text("下から上へ進みます。光る行き先をクリックして出発。", 146, 112, 735, 28, 20, Ink);
    if (ui.Hit("focus_current", 895, 110, 100, 30)) m_FocusCurrent = true;
    ui.Text("現在地へ", 895, 110, 100, 30, 19, Ink);
    if (ui.Hit("whole_map", 1000, 110, 100, 30))
    {
        m_ShowWholeMap = !m_ShowWholeMap;
        m_FocusCurrent = true;
    }
    ui.Text(m_ShowWholeMap ? "拡大表示" : "全体表示", 1000, 110, 100, 30, 19, Ink);
    ui.Text("次の行き先", 1210, 110, 400, 42, 29);

    const RunMap& map = RunRouteView::Map();
    const int rows = map.AreaCount() + 2;
    constexpr float mapHeight = 624;
    const float rowHeight = m_ShowWholeMap ? (mapHeight - 64) / rows : 120;
    const float nodeWidth = m_ShowWholeMap ? 125.0f : 220.0f;
    const float nodeHeight = m_ShowWholeMap ? rowHeight - 3 : 86.0f;
    ImGui::SetCursorScreenPos(ui.At(76, 187));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, IM_COL32(186, 143, 80, 155));
    ImGui::BeginChild("map_canvas", ui.Size(1040, mapHeight), ImGuiChildFlags_None);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 localOrigin = ImGui::GetCursorPos();
    const auto position = [&](const RunMapNode& n)
    {
        return ImVec2(origin.x + (245 + 315 * n.lane) * ui.scale,
            origin.y + (44 + (rows - 1 - (n.floor - map.StartArea())) * rowHeight) * ui.scale);
    };
    auto* draw = ImGui::GetWindowDrawList();
    // Cache reachability once per node instead of traversing the map per edge.
    std::vector<bool> reachable(map.Nodes().size());
    for (const auto& node : map.Nodes()) reachable[node.id] = map.Reachable(node.id);
    for (const auto& node : map.Nodes())
    {
        for (int nextId : node.next)
        {
            const auto* next = map.Node(nextId);
            if (!next) continue;
            const bool used = map.Visited(node.id) && map.Visited(nextId);
            const bool current = !map.Path().empty() && map.Path().back() == node.id;
            const bool open = (reachable[node.id] || current) && reachable[nextId];
            const ImU32 color = used ? Gold : open ? IM_COL32(245, 235, 214, 210) : IM_COL32(107, 102, 117, 110);
            ImVec2 from = position(node), to = position(*next);
            from.y -= nodeHeight * ui.scale * 0.28f;
            to.y += nodeHeight * ui.scale * 0.35f;
            if (used) draw->AddLine(from, to, IM_COL32(255, 188, 60, 65), 9 * ui.scale);
            draw->AddLine(from, to, color, (used ? 4 : 2.4f) * ui.scale);
        }
    }
    for (const auto& node : map.Nodes())
    {
        const ImVec2 center = position(node);
        const ImVec2 lo(center.x - nodeWidth * ui.scale * 0.5f, center.y - nodeHeight * ui.scale * 0.5f);
        const ImVec2 hi(center.x + nodeWidth * ui.scale * 0.5f, center.y + nodeHeight * ui.scale * 0.5f);
        const auto found = std::find(m_RouteNodes.begin(), m_RouteNodes.end(), node.id);
        const bool selectable = found != m_RouteNodes.end();
        const bool visited = map.Visited(node.id);
        const bool current = !map.Path().empty() && map.Path().back() == node.id;
        const bool selected = selectable && static_cast<int>(found - m_RouteNodes.begin()) == m_SelectedNode;
        const bool visible = hi.y >= ImGui::GetWindowPos().y && lo.y <= ImGui::GetWindowPos().y + ImGui::GetWindowHeight();
        if (!visible) continue;
        ImGui::SetCursorScreenPos(lo);
        ImGui::PushID(node.id);
        if (ImGui::InvisibleButton("node", ImVec2(hi.x - lo.x, hi.y - lo.y)) && selectable)
        {
            m_SelectedNode = static_cast<int>(found - m_RouteNodes.begin());
            m_MouseConfirmed = true;
        }
        const bool hovered = ImGui::IsItemHovered();
        if (selectable || current)
        {
            draw->AddRectFilled(ImVec2(lo.x - 5 * ui.scale, lo.y - 3 * ui.scale),
                ImVec2(hi.x + 5 * ui.scale, hi.y + 3 * ui.scale), IM_COL32(255, 193, 68, selected ? 85 : 44), 14 * ui.scale);
            draw->AddRect(lo, hi, selected || current ? Gold : Cream, 12 * ui.scale, 0, (selected ? 3 : 1.5f) * ui.scale);
        }
        Image(draw, RouteArt(node.type), lo, hi,
            !visited && !reachable[node.id] ? IM_COL32(110, 106, 122, 180) : IM_COL32_WHITE);
        // Special destinations reuse the matching plate; an explicit caption
        // prevents the printed rest/midboss label from misidentifying the node.
        if (node.type == StageRouteType::BossPreparation || node.type == StageRouteType::FinalBoss)
        {
            const ImVec2 labelMin(lo.x + 20 * ui.scale, center.y - 4 * ui.scale);
            const ImVec2 labelMax(hi.x - 20 * ui.scale, hi.y - 9 * ui.scale);
            draw->AddRectFilled(labelMin, labelMax, node.type == StageRouteType::FinalBoss ? IM_COL32(102, 22, 43, 255) : IM_COL32(22, 77, 45, 255), 3);
            const char* label = GetLocalizedRouteName(node.type);
            const float size = (m_ShowWholeMap ? 12 : 22) * ui.scale;
            const auto textSize = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0, label);
            draw->AddText(ImGui::GetFont(), size, ImVec2(center.x - textSize.x / 2, labelMin.y), Cream, label);
        }
        if (node.lane == 0 || node.floor >= map.StartArea() + map.AreaCount())
        {
            char area[16]; sprintf_s(area, "%02d", node.floor + 1);
            const ImVec2 badge(origin.x + 57 * ui.scale, center.y);
            const float radius = (m_ShowWholeMap ? 15 : 29) * ui.scale;
            draw->AddCircleFilled(badge, radius, IM_COL32(38, 30, 42, 245), 8);
            draw->AddCircle(badge, radius, IM_COL32(217, 172, 92, 255), 8, 2 * ui.scale);
            const float size = (m_ShowWholeMap ? 16 : 26) * ui.scale;
            const auto textSize = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0, area);
            draw->AddText(ImGui::GetFont(), size, ImVec2(badge.x - textSize.x / 2, badge.y - textSize.y / 2), Cream, area);
        }
        if (hovered)
        {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(GetLocalizedRouteName(node.type));
            ImGui::TextUnformatted(RouteHint(node.type));
            ImGui::TextUnformatted(selectable ? "クリックしてこの道へ進む" : current ? "現在地" : visited ? "通過済み" : reachable[node.id] ? "先の行き先" : "現在の経路からは進めません");
            ImGui::EndTooltip();
        }
        ImGui::PopID();
    }
    ImGui::SetCursorPos(localOrigin);
    ImGui::Dummy(ui.Size(1010, (m_ShowWholeMap ? 64 : 145) + (rows - 1) * rowHeight));
    if (m_FocusCurrent && !m_RouteNodes.empty())
    {
        const float nextY = position(*map.Node(m_RouteNodes.front())).y - origin.y;
        ImGui::SetScrollY(m_ShowWholeMap ? 0 : (std::max)(0.0f, nextY - mapHeight * ui.scale * 0.88f));
        m_FocusCurrent = false;
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(3);

    for (int index = 0; index < GetRouteNodeCount(); ++index)
    {
        const auto& node = *map.Node(m_RouteNodes[index]);
        const char* lane = node.lane == 0 ? "左の道" : node.lane == 1 ? "中央の道" : "右の道";
        const float y = 183 + index * 126.0f;
        ImGui::PushID(index);
        if (ui.Hit("route_detail", 1194, y, 432, 111))
        {
            m_SelectedNode = index;
            m_MouseConfirmed = true;
        }
        ui.Highlight(1194, y, 432, 111, index == m_SelectedNode, ImGui::IsItemHovered());
        ui.Art(RouteArt(node.type), 1204, y + 6, 172, 61);
        ui.Text(lane, 1382, y + 7, 220, 26, 22, Gold, false);
        ui.Text(GetLocalizedRouteName(node.type), 1382, y + 34, 220, 29, 23, Cream, false);
        ui.Text(RouteHint(node.type), 1210, y + 69, 394, 37, 18, Cream, false);
        ImGui::PopID();
    }
    ui.Text("ルートの見方", 1280, 585, 270, 43, 27, Ink);
    ui.Text("金色の線：通った道", 1302, 643, 308, 30, 22, Gold, false);
    ui.Text("明るい線：これから進める道", 1302, 683, 308, 30, 21, Cream, false);
    ui.Text("暗い線：選べなくなった道", 1302, 723, 308, 30, 21, Cream, false);
    ui.Text("15エリア → ボス前休憩 → 最終ボス", 1250, 791, 370, 38, 20);
    ui.Text("ホイール：先の道を見る  /  矢印：選択  Enter：出発", 185, 827, 817, 28, 19);
}

// 終了処理
void StageSelectScene::Uninit()
{
	// このシーンのオブジェクトを削除する
	for (GameObject* gameObject : m_SceneGameObjects) {
		GameRuntime::DestroyObject(gameObject);
	}
	m_SceneGameObjects.clear();
}

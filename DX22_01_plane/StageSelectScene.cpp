#pragma execution_character_set("utf-8")
#include "StageSelectScene.h"
#include "GameRuntime.h"
#include "IllustratedUi.h"
#include "RouteVisualPath.h"
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
        case StageRouteType::FinalBoss: return "assets/UI/Lounge/node_midboss.png";
        case StageRouteType::Shop: return "assets/UI/Lounge/node_shop.png";
        case StageRouteType::RestSite:
        case StageRouteType::BossPreparation: return "assets/UI/Lounge/node_rest.png";
        default: return "assets/UI/Lounge/node_battle.png";
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
	if (m_RouteFlight >= 0.0f)
    {
        m_MouseConfirmed = false;
        m_RouteFlight += std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.1f);
        if (m_RouteFlight >= 0.53f)
        {
            const int route = m_FlyingRoute;
            m_RouteFlight = -1.0f;
            ChooseRoute(route, "human");
        }
        return;
    }
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
		m_FlyingRoute = m_SelectedNode;
        m_RouteFlight = 0.0f;
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
    ui.Art("assets/UI/Lounge/map_background.png", 0, 0, 1672, 941, IM_COL32_WHITE, false);
    ui.Text("ルートマップ", 100, 77, 340, 58, 36, Cream, false);
    ui.Text("選んだボールへ、次の一打。", 520, 91, 660, 35, 23);
    ui.Text("ボールの種類", 110, 219, 260, 42, 25);
    const StageRouteType types[] = {StageRouteType::NormalBattle, StageRouteType::RestSite,
        StageRouteType::Shop, StageRouteType::MidBoss};
    for (int i=0;i<4;++i)
    {
        ui.Art(RouteArt(types[i]), 110, 281+i*77.0f, 65, 65);
        ui.Text(GetLocalizedRouteName(types[i]), 183, 290+i*77.0f, 192, 42, 23, Cream, false);
    }
    ui.Text("白球が現在地です。", 100, 664, 290, 35, 21);
    ui.Text("ホイール：先の道を見る", 100, 711, 290, 35, 18);
    ui.Text("矢印：選択  Enter：出発", 100, 750, 290, 35, 18);
    if (ui.Hit("whole_map", 102, 807, 270, 43) && m_RouteFlight<0)
    { m_ShowWholeMap=!m_ShowWholeMap; m_FocusCurrent=true; }
    ui.Text(m_ShowWholeMap ? "現在地へ戻る" : "全体を見る", 102, 807, 270, 43, 21, Gold);

    const RunMap& map=RunRouteView::Map();
    const int rows=map.AreaCount()+2;
    const float rowHeight=m_ShowWholeMap ? 610.0f/(rows+1) : 115.0f;
    const float diameter=m_ShowWholeMap ? 30.0f : 78.0f;
    ImGui::SetCursorScreenPos(ui.At(440,155));
    ImGui::BeginChild("map_canvas",ui.Size(1130,700),ImGuiChildFlags_None,
        m_RouteFlight>=0 ? ImGuiWindowFlags_NoScrollWithMouse : ImGuiWindowFlags_None);
    const auto origin=ImGui::GetCursorScreenPos();
    const auto local=ImGui::GetCursorPos();
    auto position=[&](const RunMapNode& node)
    { return ImVec2(origin.x+(180+350*node.lane)*ui.scale,
        origin.y+(45+(rows-1-(node.floor-map.StartArea()))*rowHeight)*ui.scale); };
    // Generate in canonical map coordinates, then transform for scrolling/zoom.
    auto curveFor=[&](const RunMapNode& from,const RunMapNode& to)
    {
        auto curve=RouteVisualPath::Curve(map.Seed(),from.id,to.id,
            {350.0f*from.lane,-115.0f*(from.floor-map.StartArea())},
            {350.0f*to.lane,-115.0f*(to.floor-map.StartArea())});
        for(auto& point:curve)
        {
            point.x=origin.x+(180+point.x)*ui.scale;
            point.y=origin.y+(45+(rows-1)*rowHeight+point.y*rowHeight/115.0f)*ui.scale;
        }
        return curve;
    };
    auto* draw=ImGui::GetWindowDrawList();
    std::vector<bool> reachable(map.Nodes().size());
    for (const auto& node:map.Nodes()) reachable[node.id]=map.Reachable(node.id);
    for (const auto& node:map.Nodes()) for (int id:node.next)
    {
        const auto* next=map.Node(id); if (!next) continue;
        const bool used=map.Visited(node.id)&&map.Visited(id);
        const auto curve=curveFor(node,*next);
        const ImU32 color=used ? Gold : reachable[id] ? IM_COL32(220,218,193,210) : IM_COL32(120,125,118,85);
        for (const auto& dash:RouteVisualPath::Dashes(curve,ui.scale))
            draw->AddLine(ImVec2(dash.from.x,dash.from.y),ImVec2(dash.to.x,dash.to.y),color,
                (std::max)(1.5f,ui.scale*(used?5.0f:3.8f)));
    }
    RunMapNode start;
    start.floor=m_RouteNodes.empty()?map.StartArea()-1:map.Node(m_RouteNodes.front())->floor-1;
    start.lane=1;
    const auto* current=map.Path().empty()?nullptr:map.Node(map.Path().back());
    const ImVec2 cueStart=position(current?*current:start);
    for (const auto& node:map.Nodes())
    {
        const auto center=position(node);
        if (current && current->id==node.id) continue;
        const auto found=std::find(m_RouteNodes.begin(),m_RouteNodes.end(),node.id);
        const bool available=found!=m_RouteNodes.end();
        const bool selected=available && static_cast<int>(found-m_RouteNodes.begin())==m_SelectedNode;
        const float r=diameter*ui.scale*0.5f;
        ImGui::SetCursorScreenPos(ImVec2(center.x-r,center.y-r));
        ImGui::PushID(node.id);
        if (ImGui::InvisibleButton("node",ImVec2(r*2,r*2)) && available && m_RouteFlight<0)
        { m_SelectedNode=static_cast<int>(found-m_RouteNodes.begin()); m_MouseConfirmed=true; }
        const bool hovered=ImGui::IsItemHovered();
        Image(draw,RouteArt(node.type),ImVec2(center.x-r,center.y-r),ImVec2(center.x+r,center.y+r),
            available || map.Visited(node.id) ? IM_COL32_WHITE : IM_COL32(130,140,130,150));
        if (selected || (available&&hovered))
            draw->AddCircle(center,r+4*ui.scale,Gold,48,2*ui.scale);
        if (hovered)
        { ImGui::BeginTooltip(); ImGui::TextUnformatted(GetLocalizedRouteName(node.type));
          ImGui::TextUnformatted(RouteHint(node.type)); ImGui::EndTooltip(); }
        ImGui::PopID();
    }
    ImVec2 cue=cueStart;
    if (m_RouteFlight>=0 && m_FlyingRoute>=0 && m_FlyingRoute<GetRouteNodeCount())
    {
        const auto target=position(*map.Node(m_RouteNodes[m_FlyingRoute]));
        const float t=std::clamp(m_RouteFlight/0.45f,0.0f,1.0f);
        const float ease=t*t*(3-2*t);
        const auto curve=curveFor(current?*current:start,*map.Node(m_RouteNodes[m_FlyingRoute]));
        const float sample=ease*64;
        const int i=(std::min)(63,static_cast<int>(sample));
        const float fraction=sample-i;
        cue=ImVec2(curve[i].x+(curve[i+1].x-curve[i].x)*fraction,
            curve[i].y+(curve[i+1].y-curve[i].y)*fraction);
        if(t>=1) draw->AddCircle(target,diameter*ui.scale*(0.5f+(m_RouteFlight-0.45f)*3),Gold,48,2*ui.scale);
    }
    const float r=diameter*ui.scale*0.5f;
    Image(draw,"assets/UI/Lounge/cue.png",ImVec2(cue.x-r,cue.y-r),ImVec2(cue.x+r,cue.y+r));
    if (m_RouteFlight<0) draw->AddText(ImVec2(cue.x-24*ui.scale,cue.y+r),Cream,"現在地");
    ImGui::SetCursorPos(local);
    ImGui::Dummy(ui.Size(1100,150+rows*rowHeight));
    if (m_FocusCurrent && !m_RouteNodes.empty())
    { const float y=position(*map.Node(m_RouteNodes.front())).y-origin.y;
      ImGui::SetScrollY(m_ShowWholeMap?0:(std::max)(0.0f,y-450*ui.scale)); m_FocusCurrent=false; }
    ImGui::EndChild();
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

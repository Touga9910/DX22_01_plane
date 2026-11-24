#pragma execution_character_set("utf-8")

#include "TitleScene.h"
#include "GameRuntime.h"
#include "GameSceneCommands.h"
#include "GameView.h"
#include "GameUi.h"
#include "Input.h"
#include "TitleTable.h"
#include "GameObject.h"
#include "GameWorld.h"
#include "IllustratedUi.h"
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <Windows.h>
#include "UiText.h"
#include "ProgressionProfile.h"
#include "imgui/imgui.h"

/// <summary>
/// タイトル画面を初期化する。
/// </summary>
TitleScene::TitleScene()
{
	Init();
}

/// <summary>
/// タイトル画面の終了処理を行う。
/// </summary>
TitleScene::~TitleScene()
{
	Uninit();
}

/// <summary>
/// セーブとロゴの情報を取得し、タイトル画面に表示するビリヤード台を生成する。
/// </summary>
void TitleScene::Init()
{
	m_CanContinue = GameView::CaptureTitle().validRunSave;
	m_SaveSummary = GameView::RunSaveSummary();
    auto* object=GameRuntime::CreateObject("TitleTable");
    m_Table=object->AddComponent<TitleTable>();
    m_SceneGameObjects.push_back(object);
    m_Logo=std::filesystem::exists("assets/UI/Title/logo.png");
}

/// <summary>
/// タイトルの演出を更新し、メニュー入力に応じてランの開始・再開、設定、終了確認を処理する。
/// </summary>
void TitleScene::Update()
{
    if(!GameRuntime::World().Contains(m_Table))return;
    const auto now=std::chrono::steady_clock::now();
    const float dt=std::clamp(std::chrono::duration<float>(now-m_LastTick).count(),0.0f,.1f);
    m_LastTick=now;
    m_HoverFlash=(std::max)(0.0f,m_HoverFlash-dt);
    if(m_QuitFade>=0) { m_QuitFade+=dt; if(m_QuitFade>.45f) PostQuitMessage(0); return; }
    if(m_Sequence.Active())
    {
        if(Input::GetKeyTrigger(VK_SPACE)||Input::GetKeyTrigger(VK_ESCAPE)||Input::GetKeyTrigger(VK_RETURN))m_Sequence.Skip();
        m_Sequence.Advance(dt,m_Table->RackHit());
        m_Table->Step(dt,m_Sequence.phase==TitleSequence::Phase::Break||m_Sequence.phase==TitleSequence::Phase::Fade,m_Sequence.CameraBlend());
        if(m_Sequence.ConsumeStart())
        {
            m_Table->Release();
            GameSceneCommands::StartNewRun();
            GameSceneCommands::ChangeScene(SceneType::Select);
        }
        return;
    }
    m_Table->Step(dt,false);
    if(m_ShowProgression||m_ShowQuit||ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId))return;
    const bool confirmed=m_Menu.UpdateVertical(4,false);
    if(m_Menu.WasMoved()){m_ConfirmNewRun=false;m_HoverFlash=.35f;GameSceneCommands::PlayTitleCue(false,.3f);}
    if(!confirmed)return;
    switch(m_Menu.GetIndex())
    {
    case 0:
        if(m_CanContinue&&!m_ConfirmNewRun){m_ConfirmNewRun=true;m_Message=UiText::OverwriteConfirm;return;}
        m_Sequence.Begin(GameView::CaptureTitle().transitionTiming.fadeOutSeconds);GameSceneCommands::PlayTitleCue(false,1);return;
    case 1:
        if(m_CanContinue)
        {
            const auto* previousScene=this;
            const bool loaded=GameSceneCommands::LoadSavedRun();
            if(!loaded&&GameSceneCommands::IsCurrentScene(previousScene))
            {m_Table->Awake();m_Message=GameView::SaveLoadMessage();}
        }
        return;
    case 2: GameSceneCommands::OpenTitleSettings();return;
    case 3: m_ShowQuit=true;return;
    }
}

void TitleScene::DrawUI()
{
    if(!GameRuntime::World().Contains(m_Table))return;
    using namespace IllustratedUi;
    Canvas ui("title_lounge",1600,900,IM_COL32(0,0,0,0));
    auto* d=ImGui::GetWindowDrawList();
    const auto* vp=ImGui::GetMainViewport();
    const float w=vp->WorkSize.x,h=vp->WorkSize.y;
    ui.scale=(std::min)(w*.35f/560.0f,h/900.0f);
    ui.origin=ImVec2(vp->WorkPos.x,vp->WorkPos.y+(h-900*ui.scale)*.5f);
    d->AddRectFilled(vp->WorkPos,ImVec2(vp->WorkPos.x+w*.35f,vp->WorkPos.y+h),IM_COL32(7,12,23,255));
    d->AddRectFilledMultiColor(ImVec2(vp->WorkPos.x+w*.35f,vp->WorkPos.y),ImVec2(vp->WorkPos.x+w*.43f,vp->WorkPos.y+h),IM_COL32(7,12,23,220),0,0,IM_COL32(7,12,23,220));
    if(m_Logo) ui.Art("assets/UI/Title/logo.png",42,108,492,112);
    else {ui.Text("CAROMINO",42,108,492,112,64,Gold,false);}
    ui.Text("BILLIARDS × ROGUELIKE",48,222,472,40,23,Cream,false);
    const char* names[]={m_ConfirmNewRun?"START (overwrite)":"START","CONTINUE","SETTINGS","QUIT"};
    if(!m_Sequence.Active()&&m_QuitFade<0)
    {
        for(int i=0;i<4;++i)
        {
            const float y=335+i*88.0f;
            const bool enabled=i!=1||m_CanContinue;
            const bool clicked=ui.Hit(names[i],48,y,452,72);
            const bool hovered=ImGui::IsItemHovered();
            if(i==1&&hovered)ImGui::SetTooltip("%s\n%s",UiText::ContinueRun,m_SaveSummary.c_str());
            if(enabled&&hovered&&(ImGui::GetIO().MouseDelta.x!=0||ImGui::GetIO().MouseDelta.y!=0)&&m_Menu.GetIndex()!=i)
            {m_Menu.SetIndex(i,4);m_HoverFlash=.35f;GameSceneCommands::PlayTitleCue(false,.3f);m_ConfirmNewRun=false;}
            const bool selected=m_Menu.GetIndex()==i;
            if(selected)
            {
                d->AddRectFilled(ui.At(44,y),ui.At(504,y+72),IM_COL32(213,177,96,26),8*ui.scale);
                d->AddCircleFilled(ui.At(62,y+36),4*ui.scale,Gold);
            }
            ui.Text(names[i],86,y+10,400,52,35,!enabled?IM_COL32(95,101,110,255):selected?Gold:Cream,false);
            if(enabled&&clicked&&!m_ShowProgression&&!m_ShowQuit)m_Menu.Confirm(i,4);
        }
        ui.Text(m_Message.c_str(),48,705,465,66,18,Cream,false);
        if(ui.Hit("progression",48,784,240,36)&&!m_ShowQuit)m_ShowProgression=true;
        ui.Text("アセンション・実績",48,784,250,36,19,Cream,false);
        if(ui.Hit("title_debug",320,784,180,36)&&!m_ShowQuit)GameSceneCommands::OpenDebugMode();
        ui.Text("DEBUG",320,784,180,36,18,Cream,false);
    }
    else ui.Text("START",86,345,400,52,40,Gold,false);
    ui.Text("CAROMINO / DEVELOPMENT",48,849,470,30,16,Cream,false);
    m_Table->DrawEffects(d);
    if(m_Sequence.Active())
    {
        ui.Text("SPACE / ESC / ENTER : SKIP",650,832,800,40,22);
        ImGui::SetCursorScreenPos(vp->WorkPos);
        if(ImGui::InvisibleButton("skip_title",vp->WorkSize))m_Sequence.Skip();

    }
    const float fade=m_QuitFade>=0?std::clamp(m_QuitFade/.45f,0.0f,1.0f):m_Sequence.phase==TitleSequence::Phase::Fade?m_Sequence.FadeAlpha():(m_Sequence.phase==TitleSequence::Phase::Ready||m_Sequence.phase==TitleSequence::Phase::Committed)?1.0f:0;
    if(fade>0) d->AddRectFilled(vp->WorkPos,ImVec2(vp->WorkPos.x+w,vp->WorkPos.y+h),IM_COL32(0,0,0,static_cast<int>(255*fade)));
    if(m_ShowProgression)DrawProgressionUI();
    if(m_ShowQuit&&!ImGui::IsPopupOpen("終了確認"))ImGui::OpenPopup("終了確認");
    if(ImGui::BeginPopupModal("終了確認",nullptr,ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Carominoを終了しますか？");
        if(ImGui::Button("終了する")){m_QuitFade=0;m_ShowQuit=false;ImGui::CloseCurrentPopup();}
        ImGui::SameLine();if(ImGui::Button("戻る")){m_ShowQuit=false;ImGui::CloseCurrentPopup();}
        ImGui::EndPopup();
    }
}

void TitleScene::DrawProgressionUI()
{
	const auto& profile = GameView::Progression();
	const bool hasRunSave = GameView::CaptureTitle().validRunSave;
	GameUi::PrepareWindow("progression", ImVec2(270, 60), ImVec2(740, 610));
	ImGui::Begin("アセンション・実績", nullptr, ImGuiWindowFlags_NoCollapse);
	ImGui::Text("クリア %d回 / 挑戦 %d回 / 最高到達エリア %d", profile.totalClears, profile.totalRuns, profile.highestArea);
	ImGui::SeparatorText("アセンション");
	ImGui::Text("選択中 %d / 解放済み %d", profile.selectedAscension, profile.highestUnlockedAscension);
	ImGui::TextWrapped("現在の累積ルール：");
	for (int level = 1; level <= profile.selectedAscension; ++level) ImGui::BulletText("A%d  %s", level, ProgressionProfile::AscensionRule(level));
	if (profile.selectedAscension == 0) ImGui::BulletText("A0  %s", ProgressionProfile::AscensionRule(0));
	ImGui::BeginDisabled(hasRunSave || profile.selectedAscension <= 0);
	if (ImGui::Button("難易度を下げる", ImVec2(180, 34))) GameSceneCommands::SetSelectedAscension(profile.selectedAscension - 1);
	ImGui::EndDisabled(); ImGui::SameLine();
	ImGui::BeginDisabled(hasRunSave || profile.selectedAscension >= profile.highestUnlockedAscension);
	if (ImGui::Button("難易度を上げる", ImVec2(180, 34))) GameSceneCommands::SetSelectedAscension(profile.selectedAscension + 1);
	ImGui::EndDisabled();
	if (hasRunSave) ImGui::TextDisabled("中断ランがある間は難易度を変更できません。");
	if (profile.highestUnlockedAscension < ProgressionProfile::MaximumAscension)
		ImGui::TextWrapped("A%dをクリアするとA%dを解放します。", profile.highestUnlockedAscension, profile.highestUnlockedAscension + 1);
	else ImGui::TextUnformatted("最高アセンションまで解放済みです。");
	ImGui::SeparatorText("実績と解放");
	ImGui::BeginChild("achievement_list", ImVec2(0, -55), ImGuiChildFlags_Borders);
	for (const auto& achievement : AchievementCatalog)
	{
		const bool done = profile.IsAchievementUnlocked(achievement.id);
		ImGui::TextColored(done ? ImVec4(.35f,.95f,.55f,1) : ImVec4(.72f,.72f,.72f,1), "%s  %s", done ? "達成" : "未達成", achievement.name);
		ImGui::TextWrapped("条件：%s", achievement.condition);
		ImGui::TextWrapped("解放：%s", achievement.reward);
		ImGui::Separator();
	}
	ImGui::EndChild();
	if (ImGui::Button("タイトルへ戻る", ImVec2(-1, 40))) m_ShowProgression = false;
	ImGui::End();
}


/// <summary>
/// ビリヤード台の終了処理を行い、登録したタイトル画面のオブジェクトを削除する。
/// </summary>
void TitleScene::Uninit()
{
    if(GameRuntime::World().Contains(m_Table))m_Table->Release();
    m_Table=nullptr;
	// このシーンのオブジェクトを削除する
	for (GameObject* gameObject : m_SceneGameObjects) {
		GameRuntime::DestroyObject(gameObject);
	}
	m_SceneGameObjects.clear();
}

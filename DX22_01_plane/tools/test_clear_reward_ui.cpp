// Runs the production reward screen/Canvas/GoldUi with real ImGui input.
// GPU image upload and the detailed card renderer are boundary stubs below.
#include "../ClearRewardPresentation.h"
#include "../GameRewardState.h"
#include "../GameTypes.h"
#include "../IllustratedUi.h"
#include "../BallCardPresentation.h"
#include "../PlayerBallData.h"
#include "../imgui/imgui_internal.h"
#include <cassert>
#include <iostream>

namespace
{
    int grayRegions=0,grayIcons=0,details=0,detailHovered=0;
    bool checkFieldBounds=false;
    ImVec2 detailTermCenter;
    ClearRewardPresentationModel model;
    int pending=-1;
    /// <summary>
    /// 指定したマウス入力で報酬画面を一フレーム進め、描画時に返された操作をモデルに反映する。
    /// </summary>
    ClearRewardPresentationIntent Frame(float x=-100, float y=-100, bool down=false, int button=0)
    {
        auto& io=ImGui::GetIO();
        io.AddMousePosEvent(x,y);
        io.AddMouseButtonEvent(button,down);
        grayRegions=grayIcons=0;
        ImGui::NewFrame();
        const auto intent=ClearRewardUi::Draw(model,pending);
        ImGui::Render();
        model.selectedRewardIndex=intent.selectedRewardIndex;
        model.selectedRewardBallIndex=intent.selectedRewardBallIndex;
        return intent;
    }
    /// <summary>
    /// マウスを押して離す一連の入力を報酬画面に送る。
    /// </summary>
    ClearRewardPresentationIntent Click(float x, float y, int button=0)
    {
        Frame(x,y,false,button);
        Frame(x,y,true,button);
        return Frame(x,y,false,button);
    }
    /// <summary>
    /// 指定した確認画面の表示位置を確定し、左右いずれかのボタンをクリックする。
    /// </summary>
    ClearRewardPresentationIntent PopupButton(const char* name, bool right)
    {
        Frame(); Frame(); // settle auto-size and position
        auto* window=ImGui::FindWindowByName(name);
        assert(window && window->Active);
        const float x=window->Pos.x+window->WindowPadding.x+(right?310.0f:100.0f);
        const float y=window->DC.CursorPosPrevLine.y+21;
        return Click(x,y);
    }
    /// <summary>
    /// 開いている球の詳細画面を閉じるボタンをクリックする。
    /// </summary>
    ClearRewardPresentationIntent CloseDetails()
    {
        Frame(); Frame();
        auto* window=ImGui::FindWindowByName("ボール詳細###reward_ball_details");
        assert(window && window->Active);
        return Click(window->Pos.x+window->WindowPadding.x+50,window->DC.CursorPosPrevLine.y+19);
    }
}

namespace IllustratedUi
{
    /// <summary>
    /// 画像の代わりに色付きの矩形を描画し、必要なら配置範囲を検査する。
    /// </summary>
    void Image(ImDrawList* draw,const char*,ImVec2 lo,ImVec2 hi,ImU32 tint,bool,bool)
    {
        if(checkFieldBounds) assert(lo.x>=200 && lo.y>=150 && hi.x<=1100.01f && hi.y<=825.01f);
        draw->AddRectFilled(lo,hi,tint);
    }
    /// <summary>
    /// 部分画像の描画範囲を検査し、グレー表示の回数を数えて代替の矩形を描画する。
    /// </summary>
    void ImageRegion(ImDrawList* draw,const char*,ImVec2 lo,ImVec2 hi,ImVec2,ImVec2,bool gray)
    {
        if(checkFieldBounds) assert(lo.x>=200 && lo.y>=150 && hi.x<=1100.01f && hi.y<=825.01f);
        grayRegions+=gray; draw->AddRectFilled(lo,hi,IM_COL32(100,100,100,255));
    }
}
namespace BallCardPresentation
{
    /// <summary>
    /// グレー表示として要求された球アイコンを数える。
    /// </summary>
    void DrawBallIcon(const PlayerBallData&,const ImVec2&,const ImVec2&,bool gray) { grayIcons+=gray; }
    /// <summary>
    /// 球の詳細画面のテスト表示を描画し、説明項目を操作できる位置を記録する。
    /// </summary>
    void DrawBallDetailPopup(const PlayerBallData&,int)
    {
        ++details;
        ImGui::TextUnformatted("detail card");
        const auto start=ImGui::GetCursorScreenPos();
        detailTermCenter=ImVec2(start.x+60,start.y+12);
        ImGui::InvisibleButton("detail_term",ImVec2(150,24));
        if (ImGui::IsItemHovered()) ++detailHovered;
        // The real detail renderer leaves the cursor at the next close-button
        // position. The caller must submit an item there before ending a window.
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 12.0f);
    }
}

/// <summary>
/// 報酬の終了状態、選択順、球の詳細、購入とスキップの画面操作を検証する。
/// </summary>
int main()
{
    GameRewardState state;
    state.Prepare("reward"); state.Activate();
    state.RequestConfirmation(); state.RequestSkip(); state.SetModalOpen(true);
    state.Exit();
    assert(!state.IsActive() && !state.ConsumeConfirmationRequest() && !state.ConsumeSkipRequest() && !state.IsModalOpen());
    state.Prepare("next"); state.Activate();
    state.MarkBallAcquired();
    assert(state.IsBallAcquired() && !state.IsChosen());
    state.RequestSkip(); assert(state.ConsumeSkipRequest() && !state.ConsumeSkipRequest());
    state.RequestConfirmation(); assert(state.ConsumeConfirmationRequest() && !state.ConsumeConfirmationRequest());
    state.MarkChosen(); assert(state.IsChosen());
    assert(state.IsReadyToContinue()); // ball, then bonus
    state.Prepare("next"); assert(!state.IsChosen() && !state.IsBallAcquired());
    state.MarkChosen();
    assert(!state.IsReadyToContinue()); // bonus alone must stay on the screen
    state.MarkBallAcquired();
    assert(state.IsReadyToContinue()); // bonus, then ball
    state.Prepare("next");
    state.MarkChosen(2,0,"gold");
    assert(state.IsChosen() && !state.IsReadyToContinue());
    state.MarkChosen(0,1,"ball");
    assert(state.IsChosen() && state.IsReadyToContinue()); // MCP order also preserves bonus

    ImGui::CreateContext();
    auto& io=ImGui::GetIO();
    io.IniFilename=nullptr; io.DisplaySize=ImVec2(RewardFieldLayout::CanvasWidth,RewardFieldLayout::CanvasHeight); io.DeltaTime=1.0f/60;
    unsigned char* pixels=nullptr; int width=0,height=0;
    io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    PlayerBallData balls[3];
    model.playerMoney=60; model.extraMoneyAmount=10;
    model.newBallOffers={&balls[0],&balls[1],&balls[2]};
    model.upgradeTargets={{&balls[0],15}};
    for (int i=0;i<3;++i) Frame();
    Frame(320,320); Frame(320,320);
    assert(details==0); // hover no longer opens the detail card
    auto intent=Click(1070,320,1);
    assert(intent.modalOpen && !intent.confirm && !intent.skip && pending==2);
    Frame(); Frame();
    auto* detailWindow=ImGui::FindWindowByName("ボール詳細###reward_ball_details");
    assert(detailWindow && detailWindow->Active && !(detailWindow->Flags&ImGuiWindowFlags_NoMouseInputs));
    const ImVec2 fixedPosition=detailWindow->Pos;
    const int previousDetails=details;
    intent=Frame(20,20); // leaving the offer must not close or move the detail
    assert(intent.modalOpen && details>previousDetails);
    assert(detailWindow->Pos.x==fixedPosition.x && detailWindow->Pos.y==fixedPosition.y);
    Frame(detailTermCenter.x,detailTermCenter.y); Frame(detailTermCenter.x,detailTermCenter.y);
    assert(detailHovered>0); // contents accept hover input inside the fixed modal
    intent=CloseDetails();
    assert(!intent.modalOpen && !intent.confirm && !intent.skip);
    intent=Click(320,320);
    assert(intent.modalOpen && !intent.confirm && !intent.skip && pending==0);
    intent=PopupButton("ボールの購入###reward_purchase_confirm",true);
    assert(!intent.confirm && !intent.modalOpen); // No
    intent=Click(1070,320);
    assert(intent.modalOpen && pending==2);
    intent=PopupButton("ボールの購入###reward_purchase_confirm",false);
    assert(intent.confirm && intent.selectedRewardIndex==0 && intent.selectedRewardBallIndex==2);
    model.rewardBallAcquired=true;
    Frame();
    assert(grayRegions==3 && grayIcons==3);
    intent=Click(320,320);
    assert(!intent.confirm && !intent.modalOpen); // duplicate purchase blocked
    intent=Click(1000,670);
    assert(intent.confirm && intent.selectedRewardIndex==2); // gold is immediate

    intent=Click(400,670);
    assert(intent.modalOpen && !intent.confirm);
    intent=PopupButton("強化するボールを選択###reward_upgrade_targets",true);
    assert(!intent.confirm && !intent.modalOpen); // cancel costs nothing
    intent=Click(400,670);
    assert(intent.modalOpen);
    intent=PopupButton("強化するボールを選択###reward_upgrade_targets",false);
    assert(intent.confirm && intent.selectedRewardIndex==1 && intent.selectedRewardBallIndex==0);

    // Bonus-first flow: disable only the bonus, keeping purchase and details usable.
    model.playerMoney=60; model.rewardBallAcquired=false; model.clearRewardChosen=false;
    intent=Click(1000,670);
    assert(intent.confirm && intent.selectedRewardIndex==2);
    model.clearRewardChosen=true;
    intent=Click(1000,670);
    assert(!intent.confirm);
    intent=Click(400,670);
    assert(!intent.modalOpen && !intent.confirm); // no second bonus
    intent=Click(320,320);
    assert(intent.modalOpen); // purchase remains possible after the bonus
    intent=PopupButton("ボールの購入###reward_purchase_confirm",true);
    assert(!intent.confirm && !intent.modalOpen && model.clearRewardChosen && !model.rewardBallAcquired);
    intent=Click(320,320,1);
    assert(intent.modalOpen && !intent.confirm);
    CloseDetails();
    intent=Click(320,320);
    assert(intent.modalOpen);
    intent=PopupButton("ボールの購入###reward_purchase_confirm",false);
    assert(intent.confirm && intent.selectedRewardIndex==0);

    // Upgrade-first uses the same independent completion rule as gold-first.
    model.clearRewardChosen=false; model.rewardBallAcquired=false;
    intent=Click(400,670);
    assert(intent.modalOpen && !intent.confirm);
    intent=PopupButton("強化するボールを選択###reward_upgrade_targets",false);
    assert(intent.confirm && intent.selectedRewardIndex==1);
    model.clearRewardChosen=true;
    intent=Click(1000,670);
    assert(!intent.confirm); // cannot also collect gold after choosing upgrade
    intent=Click(320,320);
    assert(intent.modalOpen);
    intent=PopupButton("ボールの購入###reward_purchase_confirm",false);
    assert(intent.confirm && intent.selectedRewardIndex==0);

    model.playerMoney=0; model.rewardBallAcquired=false;
    intent=Click(320,320);
    assert(intent.modalOpen);
    intent=PopupButton("ボールの購入###reward_purchase_confirm",false);
    assert(!intent.confirm && intent.modalOpen); // Yes disabled when unaffordable
    PopupButton("ボールの購入###reward_purchase_confirm",true);
    intent=Click(400,670);
    assert(!intent.confirm && !intent.modalOpen);
    intent=Click(720,874);
    assert(intent.skip && !intent.confirm && model.clearRewardChosen); // bonus retained
    Frame();
    model.selectedRewardIndex=0;
    io.AddKeyEvent(ImGuiKey_Enter,true);
    intent=Frame();
    assert(intent.modalOpen && !intent.confirm);
    io.AddKeyEvent(ImGuiKey_Enter,false);
    Frame();
    PopupButton("ボールの購入###reward_purchase_confirm",true);
    RelicDefinition relic{};
    relic.name="Relic"; relic.description="Description";
    model.midBossRelicSelectionActive=true; model.clearRewardChosen=false;
    model.relicOffers={&relic};
    intent=Click(320,320);
    assert(!intent.confirm && !intent.modalOpen);
    intent=Click(720,874);
    assert(intent.confirm && !intent.skip);
    Frame();
    model.midBossRelicSelectionActive=false;
    model.fieldBounds={200,150,900,675,true};
    checkFieldBounds=true;
    Frame(); Frame();
    auto* rewardWindow=ImGui::FindWindowByName("clear_reward_art");
    assert(rewardWindow && rewardWindow->Active);
    // The first quad dims the viewport. Every subsequent vertex belongs to
    // the panel or reward controls and must remain inside the fitted field.
    for (int i=4;i<rewardWindow->DrawList->VtxBuffer.Size;++i)
    {
        const auto p=rewardWindow->DrawList->VtxBuffer[i].pos;
        assert(p.x>=199.0f && p.x<=1101.0f && p.y>=149.0f && p.y<=826.0f);
    }
    const float scale=900.0f/RewardFieldLayout::CanvasWidth;
    auto scaledClick=[&](float x,float y,int button=0) {
        return Click(200+x*scale,150+y*scale,button);
    };
    model.playerMoney=60; model.clearRewardChosen=false; model.rewardBallAcquired=false;
    intent=scaledClick(320,320);
    assert(intent.modalOpen && !intent.confirm && pending==0);
    PopupButton("ボールの購入###reward_purchase_confirm",true);
    intent=scaledClick(1000,670);
    assert(intent.confirm && intent.selectedRewardIndex==2);
    intent=scaledClick(720,874);
    assert(intent.skip && !intent.confirm);
    ImGui::DestroyContext();
    std::cout << "Reward order, persistent right-click details, purchase, bonus and skip passed\n";
}

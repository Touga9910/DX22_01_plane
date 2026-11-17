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
    ImVec2 detailTermCenter;
    ClearRewardPresentationModel model;
    int pending=-1;
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
    ClearRewardPresentationIntent Click(float x, float y, int button=0)
    {
        Frame(x,y,false,button);
        Frame(x,y,true,button);
        return Frame(x,y,false,button);
    }
    ClearRewardPresentationIntent PopupButton(const char* name, bool right)
    {
        Frame(); Frame(); // settle auto-size and position
        auto* window=ImGui::FindWindowByName(name);
        assert(window && window->Active);
        const float x=window->Pos.x+window->WindowPadding.x+(right?310.0f:100.0f);
        const float y=window->DC.CursorPosPrevLine.y+21;
        return Click(x,y);
    }
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
    void Image(ImDrawList* draw,const char*,ImVec2 lo,ImVec2 hi,ImU32 tint,bool,bool)
    { draw->AddRectFilled(lo,hi,tint); }
    void ImageRegion(ImDrawList* draw,const char*,ImVec2 lo,ImVec2 hi,ImVec2,ImVec2,bool gray)
    { grayRegions+=gray; draw->AddRectFilled(lo,hi,IM_COL32(100,100,100,255)); }
}
namespace BallCardPresentation
{
    void DrawBallIcon(const PlayerBallData&,const ImVec2&,const ImVec2&,bool gray) { grayIcons+=gray; }
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
    io.IniFilename=nullptr; io.DisplaySize=ImVec2(1448,1086); io.DeltaTime=1.0f/60;
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
    intent=Click(1000,730);
    assert(intent.confirm && intent.selectedRewardIndex==2); // gold is immediate

    intent=Click(400,730);
    assert(intent.modalOpen && !intent.confirm);
    intent=PopupButton("強化するボールを選択###reward_upgrade_targets",true);
    assert(!intent.confirm && !intent.modalOpen); // cancel costs nothing
    intent=Click(400,730);
    assert(intent.modalOpen);
    intent=PopupButton("強化するボールを選択###reward_upgrade_targets",false);
    assert(intent.confirm && intent.selectedRewardIndex==1 && intent.selectedRewardBallIndex==0);

    // Bonus-first flow: disable only the bonus, keeping purchase and details usable.
    model.playerMoney=60; model.rewardBallAcquired=false; model.clearRewardChosen=false;
    intent=Click(1000,730);
    assert(intent.confirm && intent.selectedRewardIndex==2);
    model.clearRewardChosen=true;
    intent=Click(1000,730);
    assert(!intent.confirm);
    intent=Click(400,730);
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
    intent=Click(400,730);
    assert(intent.modalOpen && !intent.confirm);
    intent=PopupButton("強化するボールを選択###reward_upgrade_targets",false);
    assert(intent.confirm && intent.selectedRewardIndex==1);
    model.clearRewardChosen=true;
    intent=Click(1000,730);
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
    intent=Click(400,730);
    assert(!intent.confirm && !intent.modalOpen);
    intent=Click(720,1000);
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
    intent=Click(720,1000);
    assert(intent.confirm && !intent.skip);
    Frame();
    ImGui::DestroyContext();
    std::cout << "Reward order, persistent right-click details, purchase, bonus and skip passed\n";
}

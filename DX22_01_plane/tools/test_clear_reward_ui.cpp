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
    int grayRegions=0,grayIcons=0,details=0;
    ClearRewardPresentationModel model;
    int pending=-1;
    ClearRewardPresentationIntent Frame(float x=-100, float y=-100, bool down=false)
    {
        auto& io=ImGui::GetIO();
        io.AddMousePosEvent(x,y);
        io.AddMouseButtonEvent(0,down);
        grayRegions=grayIcons=0;
        ImGui::NewFrame();
        const auto intent=ClearRewardUi::Draw(model,pending);
        ImGui::Render();
        model.selectedRewardIndex=intent.selectedRewardIndex;
        model.selectedRewardBallIndex=intent.selectedRewardBallIndex;
        return intent;
    }
    ClearRewardPresentationIntent Click(float x, float y)
    {
        Frame(x,y);
        Frame(x,y,true);
        return Frame(x,y,false);
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
    state.Prepare("next"); assert(!state.IsChosen() && !state.IsBallAcquired());

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
    assert(details>0); // full detail renderer on hover
    auto intent=Click(320,320);
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

    model.playerMoney=0; model.rewardBallAcquired=false;
    intent=Click(320,320);
    assert(intent.modalOpen);
    intent=PopupButton("ボールの購入###reward_purchase_confirm",false);
    assert(!intent.confirm && intent.modalOpen); // Yes disabled when unaffordable
    PopupButton("ボールの購入###reward_purchase_confirm",true);
    intent=Click(400,730);
    assert(!intent.confirm && !intent.modalOpen); // unavailable upgrade stays on screen
    intent=Click(720,1000);
    assert(intent.skip && !intent.confirm);
    Frame();
    model.selectedRewardIndex=0;
    io.AddKeyEvent(ImGuiKey_Enter,true);
    intent=Frame();
    assert(intent.modalOpen && !intent.confirm); // keyboard also opens confirmation
    io.AddKeyEvent(ImGuiKey_Enter,false);
    Frame();
    PopupButton("ボールの購入###reward_purchase_confirm",true);
    RelicDefinition relic{};
    relic.name="Relic"; relic.description="Description";
    model.midBossRelicSelectionActive=true;
    model.relicOffers={&relic};
    intent=Click(320,320);
    assert(!intent.confirm && !intent.modalOpen);
    intent=Click(720,1000);
    assert(intent.confirm && !intent.skip); // preserve mid-boss relic acceptance
    Frame();
    ImGui::DestroyContext();
    std::cout << "Reward UI input, purchase confirmation, grayscale, hover, upgrade, gold and skip passed\n";
}

#include "LoungeServicePresentation.h"
#include "IllustratedUi.h"
#include "GoldUi.h"
#include "BallCardPresentation.h"
#include "PlayerBallData.h"
#include "PlayerBallText.h"
#include "PlayerBallUI.h"
#include "GameTypes.h"
#include <algorithm>
#include <string>
#pragma execution_character_set("utf-8")

namespace
{
    using namespace IllustratedUi;
    constexpr const char* Purchase = "購入の確認###lounge_purchase";
    constexpr const char* Details = "ボール詳細###lounge_details";
    constexpr const char* Deck = "ボールを選択###lounge_deck";
    constexpr const char* Heal = "休息の確認###lounge_heal";
    constexpr ImU32 Paper = IM_COL32(235, 222, 187, 255);
    constexpr ImU32 Brown = IM_COL32(61, 39, 25, 255);

    void Background(const Canvas& ui, const char* title, const char* subtitle, int money)
    {
        ui.Art("assets/UI/Lounge/services_background.png", 0, 0, 1672, 941, IM_COL32_WHITE, false);
        ui.Text(title, 420, 90, 832, 70, 48);
        ui.Text(subtitle, 320, 164, 1032, 38, 24);
        ui.Text("所持金", 1285, 104, 114, 38, 26, Gold);
        GoldUi::Draw(ImGui::GetWindowDrawList(), ui.At(1400, 102), money, 38*ui.scale, 30*ui.scale);
    }
    void Plate(const Canvas& ui, float x, float y, float w, float h, bool gray=false)
    {
        auto* draw=ImGui::GetWindowDrawList();
        draw->AddRectFilled(ui.At(x+4,y+6),ui.At(x+w+4,y+h+6),IM_COL32(0,0,0,75),12*ui.scale);
        draw->AddRectFilled(ui.At(x,y),ui.At(x+w,y+h),gray?IM_COL32(155,153,143,255):Paper,12*ui.scale);
        draw->AddRect(ui.At(x,y),ui.At(x+w,y+h),gray?IM_COL32(102,101,94,255):IM_COL32(161,134,82,255),12*ui.scale,0,2*ui.scale);
    }
    bool Button(const Canvas& ui,const char* id,const char* text,float x,float y,float w,float h,bool enabled=true,bool selected=false)
    {
        auto* draw=ImGui::GetWindowDrawList();
        draw->AddRectFilled(ui.At(x,y),ui.At(x+w,y+h),Brown,8*ui.scale);
        draw->AddRect(ui.At(x,y),ui.At(x+w,y+h),Gold,8*ui.scale,0,1.5f*ui.scale);
        const bool clicked=ui.Hit(id,x,y,w,h);
        ui.Highlight(x,y,w,h,enabled&&selected,enabled&&ImGui::IsItemHovered(),enabled);
        ui.Text(text,x+8,y+6,w-16,h-12,26,enabled?Cream:IM_COL32(150,145,130,255));
        return enabled&&clicked;
    }
    void ModalStyle()
    {
        ImGui::PushStyleColor(ImGuiCol_PopupBg,ImVec4(.08f,.14f,.115f,1));
        ImGui::PushStyleColor(ImGuiCol_Border,ImVec4(.65f,.52f,.31f,1));
        ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.25f,.16f,.10f,1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4(.40f,.28f,.15f,1));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,ImVec4(.50f,.35f,.20f,1));
        ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(1,.94f,.78f,1));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(20,18));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,10);
    }
    void ModalSize(float width,float height)
    {
        const auto* vp=ImGui::GetMainViewport();
        ImGui::SetNextWindowSize(ImVec2((std::min)(width,vp->WorkSize.x-32),(std::min)(height,vp->WorkSize.y-32)),ImGuiCond_Appearing);
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x+vp->WorkSize.x*.5f,vp->WorkPos.y+vp->WorkSize.y*.5f),ImGuiCond_Appearing,ImVec2(.5f,.5f));
    }
    bool ModalButton(const char* label,bool enabled=true)
    {
        ImGui::BeginDisabled(!enabled);
        const bool clicked=ImGui::Button(label,ImVec2(180,40));
        ImGui::EndDisabled();
        return enabled&&clicked;
    }
    void DetailModal(const std::vector<const PlayerBallData*>& balls,int index)
    {
        ModalSize(780,470);
        if(ImGui::BeginPopupModal(Details,nullptr,ImGuiWindowFlags_NoSavedSettings))
        {
            const auto* ball=index>=0&&index<static_cast<int>(balls.size())?balls[index]:nullptr;
            if(ball)
            {
                BallCardPresentation::DrawBallDetailPopup(*ball,ball->status.attack);
                // The shared renderer positions the cursor; submit an item there.
                ImGui::Dummy(ImVec2(1,1));
            }
            if(ModalButton("閉じる")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
    template<class Predicate>
    int FirstEligible(const std::vector<const PlayerBallData*>& deck,int current,Predicate allowed)
    {
        if(current>=0&&current<static_cast<int>(deck.size())&&deck[current]&&allowed(*deck[current])) return current;
        for(int i=0;i<static_cast<int>(deck.size());++i) if(deck[i]&&allowed(*deck[i])) return i;
        return -1;
    }
    bool DeckModal(const std::vector<const PlayerBallData*>& deck, LoungeServiceUi::State& state,bool upgrade,bool allowed,int price=0)
    {
        bool confirmed=false;
        ModalSize(670,680);
        if(ImGui::BeginPopupModal(Deck,nullptr,ImGuiWindowFlags_NoSavedSettings))
        {
            ImGui::TextUnformatted(upgrade?"強化するボールを1個選んでください。":"削除するボールを1個選んでください。");
            if(!upgrade) ImGui::Text("削除費用：%d ゴールド",price);
            ImGui::BeginChild("deck_options",ImVec2(0,-115));
            for(int i=0;i<static_cast<int>(deck.size());++i)
            {
                const auto* ball=deck[i]; if(!ball) continue;
                ImGui::PushID(i);
                ImGui::BeginDisabled(upgrade&&!ball->CanUpgrade());
                if(PlayerBallUI::Select(*ball,i==state.deckIndex)) state.deckIndex=i;
                ImGui::EndDisabled();
                if(i==state.deckIndex)
                {
                    const auto description=upgrade?PlayerBallText::GetUpgradePreview(*ball):std::string(PlayerBallText::GetDescription(ball->definitionId));
                    ImGui::TextWrapped("%s",description.c_str());
                }
                ImGui::Separator(); ImGui::PopID();
            }
            ImGui::EndChild();
            const auto* selected=state.deckIndex>=0&&state.deckIndex<static_cast<int>(deck.size())?deck[state.deckIndex]:nullptr;
            if(ModalButton(upgrade?"このボールを強化":"このボールを削除",allowed&&selected&&(!upgrade||selected->CanUpgrade())))
            { confirmed=true; ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if(ModalButton("戻る")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        return confirmed;
    }
    void FinishModals(LoungeServiceUi::State& state)
    {
        state.modalOpen=ImGui::IsPopupOpen(Purchase)||ImGui::IsPopupOpen(Details)||ImGui::IsPopupOpen(Deck)||ImGui::IsPopupOpen(Heal);
        ImGui::PopStyleVar(2); ImGui::PopStyleColor(6);
    }
}

LoungeServiceUi::Intent LoungeServiceUi::DrawShop(const ShopModel& m,State& state)
{
    Canvas ui("lounge_shop",1672,941,IM_COL32(20,24,22,255));
    Intent intent;
    Background(ui,"ショップ","所持金の範囲で、自由に購入できます。",m.money);
    ui.Text("ボール購入",138,213,400,42,30,Cream,false);
    ui.Text("レリック購入",138,482,400,42,30,Cream,false);
    for(int i=0;i<3;++i)
    {
        const float x=146+i*468.0f;
        const auto* ball=i<static_cast<int>(m.balls.size())?m.balls[i]:nullptr;
        Plate(ui,x,265,440,200);
        if(ball)
        {
            BallCardPresentation::DrawBallIcon(*ball,ui.At(x+22,285),ui.Size(132,132));
            const std::string name=std::string(PlayerBallText::GetName(ball->definitionId))+" +"+std::to_string(ball->upgradeLevel);
            ui.Text(name.c_str(),x+162,282,260,54,27,Brown);
            GoldUi::Draw(ImGui::GetWindowDrawList(),ui.At(x+257,386),m.ballPrice,38*ui.scale,30*ui.scale,Brown);
        }
        else ui.Text("入荷なし",x+30,318,380,50,28,Brown);
        ImGui::PushID(i);
        const bool clicked=ui.Hit("ball_offer",x,265,440,200);
        const bool hovered=ImGui::IsItemHovered();
        ui.Highlight(x,265,440,200,m.selectedAction==0&&m.selectedBall==i,hovered,ball!=nullptr);
        if(ball&&hovered&&ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        { state.detailIndex=i; ImGui::PopID(); ImGui::OpenPopup(Details); }
        else
        {
            ImGui::PopID();
            if(clicked&&ball)
            { state.pendingAction=0; state.pendingIndex=i; ImGui::OpenPopup(Purchase); }
        }
        const auto* relic=i<static_cast<int>(m.relics.size())?m.relics[i]:nullptr;
        const bool owned=i<static_cast<int>(m.relicOwned.size())&&m.relicOwned[i];
        Plate(ui,x,535,440,200,owned);
        if(relic)
        {
            const std::string art="assets/UI/Lounge/service_relic_"+std::to_string(static_cast<int>(relic->type)%3)+".png";
            IllustratedUi::Image(ImGui::GetWindowDrawList(),art.c_str(),ui.At(x+20,551),ui.At(x+158,718),IM_COL32_WHITE,true,owned);
            ui.Text(relic->name,x+165,548,255,64,27,Brown);
            GoldUi::Draw(ImGui::GetWindowDrawList(),ui.At(x+257,657),relic->price,38*ui.scale,30*ui.scale,Brown,owned);
            if(owned) ui.Text("所持済み",x+165,611,255,36,23,Brown);
        }
        else ui.Text("入荷なし",x+30,588,380,50,28,Brown);
        ImGui::PushID(10+i);
        const bool relicClicked=ui.Hit("relic_offer",x,535,440,200);
        const bool relicHovered=ImGui::IsItemHovered();
        if(!owned) ui.Highlight(x,535,440,200,m.selectedAction==2&&m.selectedRelic==i,relicHovered,relic!=nullptr);
        if(relicHovered&&relic) ImGui::SetTooltip("%s\n%s",relic->name,relic->description);
        ImGui::PopID();
        if(relicClicked&&relic&&!owned)
        { state.pendingAction=2; state.pendingIndex=i; ImGui::OpenPopup(Purchase); }
    }
    const bool canRemove=m.money>=m.removePrice&&static_cast<int>(m.deck.size())>m.minimumDeck;
    ui.Art("assets/UI/Lounge/service_remove.png",110,765,165,115);
    if(Button(ui,"remove_ball","",292,790,320,60,canRemove,m.selectedAction==1))
    {
        state.deckIndex=FirstEligible(m.deck,state.deckIndex,[](const auto&){return true;});
        ImGui::OpenPopup(Deck);
    }
    ui.Text("ボール削除",300,800,195,38,26);
    GoldUi::Draw(ImGui::GetWindowDrawList(),ui.At(500,803),m.removePrice,32*ui.scale,26*ui.scale);
    if(ImGui::IsItemHovered()&&!canRemove) ImGui::SetTooltip("所持金が不足しているか、デッキが最低枚数です（最低%d個）。",m.minimumDeck);
    ui.Text(m.message.empty()?"購入後も、続けて別の商品を選べます。":m.message.c_str(),650,785,500,75,23);
    if(Button(ui,"leave_shop","マップへ戻る",1190,790,330,60,true,m.selectedAction==3)) intent.action=3;
    ui.Text("左クリック：購入確認　　右クリック：ボール詳細",350,750,972,30,22);

    ModalStyle();
    ModalSize(560,300);
    if(ImGui::BeginPopupModal(Purchase,nullptr,ImGuiWindowFlags_NoSavedSettings))
    {
        const int i=state.pendingIndex;
        const auto* ball=state.pendingAction==0&&i>=0&&i<static_cast<int>(m.balls.size())?m.balls[i]:nullptr;
        const auto* relic=state.pendingAction==2&&i>=0&&i<static_cast<int>(m.relics.size())?m.relics[i]:nullptr;
        const bool owned=relic&&i<static_cast<int>(m.relicOwned.size())&&m.relicOwned[i];
        const int cost=relic?relic->price:m.ballPrice;
        ImGui::TextWrapped("%s",ball?PlayerBallText::GetName(ball->definitionId):relic?relic->name:"候補がありません");
        if(relic) ImGui::TextWrapped("%s",relic->description);
        ImGui::Text("購入費用：%d ゴールド / 所持金：%d",cost,m.money);
        ImGui::TextUnformatted("この商品を購入しますか？");
        if(m.money<cost) ImGui::TextUnformatted("所持金が不足しています。");
        if(ModalButton("購入する",(ball||relic)&&!owned&&m.money>=cost))
        { intent={state.pendingAction,i}; ImGui::CloseCurrentPopup(); }
        ImGui::SameLine(); if(ModalButton("戻る")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    DetailModal(m.balls,state.detailIndex);
    if(DeckModal(m.deck,state,false,canRemove,m.removePrice)) intent={1,state.deckIndex};
    FinishModals(state);
    return intent;
}

LoungeServiceUi::Intent LoungeServiceUi::DrawRest(const RestModel& m,State& state)
{
    Canvas ui("lounge_rest",1672,941,IM_COL32(20,24,22,255));
    Intent intent;
    Background(ui,m.bossPreparation?"最終準備":"休憩所",m.bossPreparation?"この休憩の後、最終ボスへ。":"キューを置いて、ひと息。",m.money);
    ui.Text(("HP "+std::to_string(m.hp)+" / "+std::to_string(m.maxHp)).c_str(),125,106,270,42,30,Cream,false);
    auto* draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(ui.At(125,161),ui.At(395,177),Brown,5*ui.scale);
    const float ratio=m.maxHp>0?std::clamp(static_cast<float>(m.hp)/m.maxHp,0.0f,1.0f):0.0f;
    draw->AddRectFilled(ui.At(125,161),ui.At(125+270*ratio,177),IM_COL32(164,57,54,255),5*ui.scale);
    ui.Art("assets/UI/Lounge/service_coffee.png",340,238,340,210);
    ui.Art("assets/UI/Lounge/service_anvil.png",1000,238,310,210);
    for(int i=0;i<2;++i)
    {
        const float x=i==0?265.0f:907.0f;
        Plate(ui,x,455,500,278,m.used);
        if(!m.used) ui.Highlight(x,455,500,278,m.selectedAction==i,false);
    }
    ui.Text("休息",290,470,450,58,43,Brown);
    ui.Text("HPを回復する",290,537,450,40,27,Brown);
    ui.Text(("+"+std::to_string(m.healAmount)+" HP").c_str(),290,584,450,52,42,IM_COL32(25,93,60,255));
    ui.Text("ボール強化",932,470,450,58,40,Brown);
    ui.Text("所持ボールを1個強化する",932,537,450,40,27,Brown);
    ui.Text("UP",932,584,450,52,37,IM_COL32(145,107,44,255));
    ui.Text("OR",783,543,106,70,44,Gold);
    draw->AddLine(ui.At(836,420),ui.At(836,525),Gold,ui.scale);
    draw->AddLine(ui.At(836,630),ui.At(836,727),Gold,ui.scale);
    const bool canUpgrade=!m.used&&std::any_of(m.deck.begin(),m.deck.end(),[](const auto* b){return b&&b->CanUpgrade();});
    if(Button(ui,"rest_heal",m.used?"利用済み":"選択する",350,658,330,54,!m.used&&m.canHeal)) ImGui::OpenPopup(Heal);
    if(Button(ui,"rest_upgrade",m.used?"利用済み":"選択する",992,658,330,54,canUpgrade))
    {
        state.deckIndex=FirstEligible(m.deck,state.deckIndex,[](const auto& b){return b.CanUpgrade();});
        ImGui::OpenPopup(Deck);
    }
    ui.Text(m.used?"休憩は利用済みです。次へ進めます。":"どちらか1つだけ選べます",320,765,1032,43,32);
    ui.Text(m.message.empty()?(m.used?"回復・強化は、これ以上利用できません。":"選択すると、もう一方は利用できません。"):m.message.c_str(),400,815,790,42,23);
    const bool canLeave=m.used||!m.availableBenefit;
    if(Button(ui,"leave_rest","次へ進む",1300,820,260,56,canLeave,m.selectedAction==2)) intent.action=2;
    if(ImGui::IsItemHovered()&&!canLeave) ImGui::SetTooltip("回復か強化を1つ選んでから進みます。");
    ui.Text("左クリック：選択内容を確認",125,848,600,32,22,Cream,false);
    ModalStyle();
    ModalSize(540,250);
    if(ImGui::BeginPopupModal(Heal,nullptr,ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::Text("HPを%d回復します（最大HPの%d%%）。",m.healAmount,m.healPercent);
        ImGui::TextUnformatted("この休憩所では、ボール強化を選べなくなります。");
        if(ModalButton("休息する",!m.used&&m.canHeal)) { intent.action=0; ImGui::CloseCurrentPopup(); }
        ImGui::SameLine(); if(ModalButton("戻る")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if(DeckModal(m.deck,state,true,canUpgrade)) intent={1,state.deckIndex};
    FinishModals(state);
    return intent;
}

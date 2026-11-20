// Links the production UI and ImGui. Only GPU textures / ball detail artwork
// are boundary stubs; clicks, popups, models, and returned intents are real.
#include "../LoungeServicePresentation.h"
#include "../PlayerBallData.h"
#include "../GameTypes.h"
#include "../BallCardPresentation.h"
#include "../IllustratedUi.h"
#include "../imgui/imgui_internal.h"
#include <cassert>
#include <iostream>

namespace IllustratedUi
{
    void Image(ImDrawList* d,const char*,ImVec2 a,ImVec2 b,ImU32 color,bool,bool) { d->AddRectFilled(a,b,color); }
}
namespace BallCardPresentation
{
    void DrawBallIcon(const PlayerBallData&,const ImVec2&,const ImVec2&,bool) {}
    void DrawBallDetailPopup(const PlayerBallData&,int) { ImGui::TextUnformatted("Detail"); }
}
using namespace LoungeServiceUi;
ShopModel shop;
RestModel rest;
State state;
bool atRest=false;
float screenScale=1;
Intent Frame(float x=-100,float y=-100,bool down=false,int button=0)
{
    auto& io=ImGui::GetIO();
    io.AddMousePosEvent(x,y); io.AddMouseButtonEvent(button,down);
    ImGui::NewFrame();
    const auto intent=atRest?DrawRest(rest,state):DrawShop(shop,state);
    ImGui::Render(); return intent;
}
Intent Click(float x,float y,int button=0)
{
    x*=screenScale; y*=screenScale;
    Frame(x,y,false,button); Frame(x,y,true,button); return Frame(x,y,false,button);
}
Intent Popup(const char* name,bool confirm)
{
    Frame(); Frame();
    auto* window=ImGui::FindWindowByName(name); assert(window&&window->Active);
    const float x=window->Pos.x+window->WindowPadding.x+(confirm?85:280);
    const float y=window->DC.CursorPosPrevLine.y+20;
    Frame(x,y); Frame(x,y,true); return Frame(x,y);
}
int main()
{
    ImGui::CreateContext(); auto& io=ImGui::GetIO();
    io.IniFilename=nullptr; io.DisplaySize={1672,941}; io.DeltaTime=1.0f/60;
    unsigned char* pixels; int w,h; io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
    PlayerBallData balls[6]; for(auto& b:balls) { b.definitionId="player_standard"; }
    shop.money=120; shop.minimumDeck=5; shop.balls={&balls[0],&balls[1],&balls[2]};
    for(auto& b:balls) shop.deck.push_back(&b);
    shop.relics={&RelicCatalog[0],&RelicCatalog[1],&RelicCatalog[2]}; shop.relicOwned={false,false,false};
    Frame(); Frame();
    auto intent=Click(370,350); assert(state.modalOpen&&intent.action==-1);
    intent=Popup("購入の確認###lounge_purchase",false); assert(intent.action==-1&&!state.modalOpen);
    Click(370,350); intent=Popup("購入の確認###lounge_purchase",true); assert(intent.action==0&&intent.index==0);
    // Ball purchases remain repeatable, and buying never forces a scene exit.
    shop.money-=shop.ballPrice;
    Click(830,350); intent=Popup("購入の確認###lounge_purchase",true); assert(intent.action==0&&intent.index==1);
    Click(1300,620); intent=Popup("購入の確認###lounge_purchase",true); assert(intent.action==2&&intent.index==2);
    shop.relicOwned[2]=true; intent=Click(1300,620); assert(intent.action==-1&&!state.modalOpen);
    shop.money=0; Click(370,350); intent=Popup("購入の確認###lounge_purchase",true); assert(intent.action==-1&&state.modalOpen);
    Popup("購入の確認###lounge_purchase",false);
    shop.money=120;
    Click(450,820); intent=Popup("ボールを選択###lounge_deck",true); assert(intent.action==1&&intent.index==0);
    shop.deck.resize(5); intent=Click(450,820); assert(intent.action==-1&&!state.modalOpen);
    intent=Click(1350,820); assert(intent.action==3);
    Click(370,350,1); assert(state.modalOpen);
    Frame(); Frame();
    auto* detail=ImGui::FindWindowByName("ボール詳細###lounge_details"); assert(detail&&detail->Active);
    float x=detail->Pos.x+detail->WindowPadding.x+80, y=detail->DC.CursorPosPrevLine.y+20;
    Frame(x,y); Frame(x,y,true); Frame(x,y); assert(!state.modalOpen);
    atRest=true; state={}; rest.hp=28; rest.maxHp=45; rest.canHeal=true; rest.availableBenefit=true;
    rest.healAmount=12; rest.healPercent=25; rest.deck=shop.deck;
    Frame(); Frame();
    intent=Click(1400,860); assert(intent.action==-1); // cannot discard available rest bonus
    Click(500,685); intent=Popup("休息の確認###lounge_heal",false); assert(intent.action==-1&&!state.modalOpen);
    Click(500,685); intent=Popup("休息の確認###lounge_heal",true); assert(intent.action==0);
    rest.used=true;
    intent=Click(500,685); assert(intent.action==-1&&!state.modalOpen);
    intent=Click(1130,685); assert(intent.action==-1&&!state.modalOpen);
    intent=Click(1400,860); assert(intent.action==2);
    rest.used=false; rest.hp=45; rest.canHeal=false;
    intent=Click(500,685); assert(intent.action==-1&&!state.modalOpen);
    Click(1130,685); intent=Popup("ボールを選択###lounge_deck",true); assert(intent.action==1);
    rest.availableBenefit=false; for(auto& b:balls) b.upgradeLevel=PlayerBallData::MaxUpgradeLevel;
    intent=Click(1130,685); assert(intent.action==-1&&!state.modalOpen);
    intent=Click(1400,860); assert(intent.action==2);
    atRest=false; io.DisplaySize={1003.2f,564.6f}; screenScale=.6f; Frame(); Frame();
    Click(370,350); intent=Popup("購入の確認###lounge_purchase",true); assert(intent.action==0);
    ImGui::DestroyContext();
    std::cout<<"PASS: shop multi-buy, owned/funds/deck guards, details, rest exclusivity, scaled input\n";
}

#include "../PlayerAuraEffect.h"
#include "../PlayerBallText.h"
#include "../imgui/imgui.h"
#include <cassert>
#include <iostream>
#include <limits>

using Position = PlayerAuraEffect::Position;
float Distance(Position a, Position b)
{ return std::hypot(a.x-b.x,a.y-b.y,a.z-b.z); }
bool Project(const Position& p, ImVec2& screen)
{ screen=ImVec2(400+p.x*10,300+p.z*10); return true; }

int main()
{
    PlayerBallData ball;
    const auto blue=PlayerBallText::GetColor(ball);
    PlayerAuraEffect at30,at120;
    at30.Update({},2,blue,1,0);
    at120.Update({},2,blue,1,0);
    for (int i=0;i<30;++i) at30.Update({1,0,0},2,blue,1,1.0f/30);
    for (int i=0;i<120;++i) at120.Update({1,0,0},2,blue,1,1.0f/120);
    assert(Distance(at30.FollowPosition(),at120.FollowPosition())<0.00001f);
    assert(Distance(at30.FollowPosition(),{1,0,0})<0.00001f);
    const auto before=at30.FollowPosition();
    const float phase=at30.Phase();
    at30.Update({1,0,0},2,blue,1,0);
    assert(Distance(at30.FollowPosition(),before)==0 && at30.Phase()==phase);
    at30.Update({10,0,0},2,blue,1,1.0f/60);
    assert(Distance(at30.FollowPosition(),{10,0,0})<=1.60001f);
    at30.Update({-2,0,0},2,blue,1,1.0f/60);
    assert(Distance(at30.FollowPosition(),{-2,0,0})<=1.60001f);
    at30.Update({100,0,0},2,blue,1,1.0f/60); // teleport/return
    assert(at30.FollowPosition().x==100);
    ball.category=BallCategory::Heavy;
    const auto orange=PlayerBallText::GetColor(ball);
    at30.Update({101,0,0},2,orange,1,1.0f/60); // category change
    assert(at30.FollowPosition().x==101);
    at30.Update({102,0,0},2,orange,2,1.0f/60); // new player object
    assert(at30.FollowPosition().x==102);
    at30.Reset();
    assert(!at30.IsActive());
    at30.Update({},2,blue,1,std::numeric_limits<float>::quiet_NaN());
    assert(at30.IsActive() && std::isfinite(at30.Phase()));
    at30.Update({std::numeric_limits<float>::quiet_NaN(),0,0},2,blue,1,0);
    assert(!at30.IsActive());

    ImGui::CreateContext();
    auto& io=ImGui::GetIO();
    io.IniFilename=nullptr;
    io.DisplaySize=ImVec2(800,600);
    io.DeltaTime=1.0f/60;
    unsigned char* pixels=nullptr; int width=0,height=0;
    io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    for (BallCategory category : {BallCategory::Standard,BallCategory::Heavy,
        BallCategory::Pierce,BallCategory::Bounce,BallCategory::Anchor})
    {
        ball.category=category;
        const auto color=PlayerBallText::GetColor(ball);
        PlayerAuraEffect effect;
        effect.Update({},2,color,1,0);
        ImGui::NewFrame();
        effect.Draw({},20,Project);
        ImGui::Render();
        const auto* data=ImGui::GetDrawData();
        assert(data->TotalVtxCount>0 && data->TotalVtxCount<10000);
        const int stationaryVertices=data->TotalVtxCount;
        const ImU32 rgb=IM_COL32(static_cast<int>(color[0]*255),
            static_cast<int>(color[1]*255),static_cast<int>(color[2]*255),0)&0x00ffffffu;
        for (int list=0;list<data->CmdListsCount;++list)
            for (const auto& v : data->CmdLists[list]->VtxBuffer)
            {
                const float distance=std::hypot(v.pos.x-400,v.pos.y-300);
                assert(distance>19 && distance<40); // center stays clear
                assert((v.col&0x00ffffffu)==rgb);
            }
        effect.Update({1,0,0},2,color,1,1.0f/60);
        ImGui::NewFrame();
        effect.Draw({1,0,0},20,Project);
        ImGui::Render();
        data=ImGui::GetDrawData();
        assert(data->TotalVtxCount>stationaryVertices); // lagging light exists
        for (int list=0;list<data->CmdListsCount;++list)
            for (const auto& v : data->CmdLists[list]->VtxBuffer)
            {
                const float distance=std::hypot(v.pos.x-410,v.pos.y-300);
                assert(distance>18 && distance<56); // short follow, clear ball center
            }
    }
    ImGui::NewFrame();
    PlayerAuraEffect hidden;
    hidden.Draw({},20,Project);
    ImGui::Render();
    assert(ImGui::GetDrawData()->TotalVtxCount==0);
    ImGui::DestroyContext();
    std::cout << "Player aura motion, reset, palette and actual ImGui geometry passed\n";
}

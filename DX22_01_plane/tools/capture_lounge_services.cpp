// Offscreen preview of the production lounge UI. This does NOT start DX11.
// ImGui triangles and PNG sprites are rasterized on the CPU; ball artwork is
// supplied through the image boundary instead of DirectX's texture loader.
#include "../LoungeServicePresentation.h"
#include "../IllustratedUi.h"
#include "../BallCardPresentation.h"
#include "../PlayerBallData.h"
#include "../GameTypes.h"
#include "../PlayerBallText.h"
#include "../imgui/imgui.h"
#define STB_IMAGE_IMPLEMENTATION
#include "../stb_image.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace
{
    struct Texture { int w=0,h=0; std::vector<unsigned char> rgba; ImVec2 uv0{0,0},uv1{1,1}; };
    std::map<ImTextureID,Texture> textures;
    std::map<std::string,ImTextureID> ids;
    ImTextureID Load(const char* path,bool gray)
    {
        const std::string key=std::string(path)+(gray?"#gray":"");
        if(ids.count(key)) return ids[key];
        Texture t; int channels;
        auto* pixels=stbi_load(path,&t.w,&t.h,&channels,4);
        if(!pixels) throw std::runtime_error(std::string("Cannot load ")+path);
        t.rgba.assign(pixels,pixels+t.w*t.h*4); stbi_image_free(pixels);
        int left=t.w,top=t.h,right=0,bottom=0;
        for(int y=0;y<t.h;++y)for(int x=0;x<t.w;++x)
        {
            const int i=(y*t.w+x)*4;
            const bool gutter=key.find("/Cards/card_")!=std::string::npos&&t.rgba[i]<=3&&t.rgba[i+1]<=3&&t.rgba[i+2]<=3;
            if(t.rgba[i+3]>=24&&!gutter) {left=std::min(left,x);top=std::min(top,y);right=std::max(right,x);bottom=std::max(bottom,y);}
            if(gray) { const auto v=(54u*t.rgba[i]+183u*t.rgba[i+1]+19u*t.rgba[i+2])/256u; t.rgba[i]=t.rgba[i+1]=t.rgba[i+2]=static_cast<unsigned char>(v); }
        }
        t.uv0={static_cast<float>(std::max(0,left-2))/t.w,static_cast<float>(std::max(0,top-2))/t.h};
        t.uv1={static_cast<float>(std::min(t.w,right+3))/t.w,static_cast<float>(std::min(t.h,bottom+3))/t.h};
        const ImTextureID id=textures.size()+1; textures[id]=std::move(t); ids[key]=id; return id;
    }
    void Capture(const std::filesystem::path& path)
    {
        const auto* data=ImGui::GetDrawData();
        const int w=static_cast<int>(data->DisplaySize.x),h=static_cast<int>(data->DisplaySize.y);
        std::vector<unsigned char> rgb(w*h*3,20);
        for(const auto* list:data->CmdLists)
        for(const auto& cmd:list->CmdBuffer)
        {
            if(cmd.UserCallback) { cmd.UserCallback(list,&cmd); continue; }
            const auto& tex=textures.at(cmd.GetTexID());
            for(unsigned e=0;e<cmd.ElemCount;e+=3)
            {
                std::array<ImDrawVert,3> v;
                for(int k=0;k<3;++k) v[k]=list->VtxBuffer[list->IdxBuffer[cmd.IdxOffset+e+k]+cmd.VtxOffset];
                const float area=(v[1].pos.x-v[0].pos.x)*(v[2].pos.y-v[0].pos.y)-(v[1].pos.y-v[0].pos.y)*(v[2].pos.x-v[0].pos.x);
                if(std::abs(area)<.00001f) continue;
                const int x0=std::max({0,static_cast<int>(std::floor(std::min({v[0].pos.x,v[1].pos.x,v[2].pos.x}))),static_cast<int>(std::ceil(cmd.ClipRect.x))});
                const int y0=std::max({0,static_cast<int>(std::floor(std::min({v[0].pos.y,v[1].pos.y,v[2].pos.y}))),static_cast<int>(std::ceil(cmd.ClipRect.y))});
                const int x1=std::min({w,static_cast<int>(std::ceil(std::max({v[0].pos.x,v[1].pos.x,v[2].pos.x}))),static_cast<int>(cmd.ClipRect.z)});
                const int y1=std::min({h,static_cast<int>(std::ceil(std::max({v[0].pos.y,v[1].pos.y,v[2].pos.y}))),static_cast<int>(cmd.ClipRect.w)});
                for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x)
                {
                    const float px=x+.5f,py=y+.5f;
                    const float a=((v[1].pos.x-px)*(v[2].pos.y-py)-(v[1].pos.y-py)*(v[2].pos.x-px))/area;
                    const float b=((v[2].pos.x-px)*(v[0].pos.y-py)-(v[2].pos.y-py)*(v[0].pos.x-px))/area;
                    const float c=1-a-b;
                    if(a<0||b<0||c<0) continue;
                    const float u=a*v[0].uv.x+b*v[1].uv.x+c*v[2].uv.x, vv=a*v[0].uv.y+b*v[1].uv.y+c*v[2].uv.y;
                    const float fx=u*tex.w-.5f,fy=vv*tex.h-.5f;
                    const int tx=static_cast<int>(std::floor(fx)),ty=static_cast<int>(std::floor(fy));
                    const float dx=fx-tx,dy=fy-ty;
                    std::array<float,4> sample{};
                    for(int yy=0;yy<2;++yy)for(int xx=0;xx<2;++xx)
                    {
                        const int offset=(std::clamp(ty+yy,0,tex.h-1)*tex.w+std::clamp(tx+xx,0,tex.w-1))*4;
                        const float weight=(xx?dx:1-dx)*(yy?dy:1-dy);
                        for(int k=0;k<4;++k) sample[k]+=tex.rgba[offset+k]*weight;
                    }
                    const auto* t=sample.data();
                    const float alpha=(a*(v[0].col>>24)+b*(v[1].col>>24)+c*(v[2].col>>24))*t[3]/65025.0f;
                    for(int k=0;k<3;++k)
                    {
                        const float col=a*((v[0].col>>(8*k))&255)+b*((v[1].col>>(8*k))&255)+c*((v[2].col>>(8*k))&255);
                        auto& dest=rgb[(y*w+x)*3+k];
                        dest=static_cast<unsigned char>(std::clamp(col*t[k]/255*alpha+dest*(1-alpha),0.0f,255.0f));
                    }
                }
            }
        }
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path,std::ios::binary); out<<"P6\n"<<w<<' '<<h<<"\n255\n"; out.write(reinterpret_cast<const char*>(rgb.data()),rgb.size());
    }
}
namespace IllustratedUi
{
    void Image(ImDrawList* draw,const char* path,ImVec2 lo,ImVec2 hi,ImU32 tint,bool trim,bool gray)
    {
        const auto id=Load(path,gray); const auto& t=textures.at(id);
        const auto uv0=trim?t.uv0:ImVec2(0,0),uv1=trim?t.uv1:ImVec2(1,1);
        const float w=t.w*(uv1.x-uv0.x),h=t.h*(uv1.y-uv0.y);
        const float scale=std::min((hi.x-lo.x)/w,(hi.y-lo.y)/h);
        ImVec2 a{lo.x+((hi.x-lo.x)-w*scale)*.5f,lo.y+((hi.y-lo.y)-h*scale)*.5f};
        draw->AddImage(ImTextureRef(id),a,ImVec2(a.x+w*scale,a.y+h*scale),uv0,uv1,tint);
    }
}
namespace BallCardPresentation
{
    void DrawBallIcon(const PlayerBallData& ball,const ImVec2& p,const ImVec2& size,bool gray)
    {
        const char* file=ball.category==BallCategory::Heavy?"card_heavy.png":ball.category==BallCategory::Pierce?"card_pierce.png":ball.category==BallCategory::Bounce?"card_reflect.png":"card_anchor_green.png";
        const std::string path=std::string("assets/UI/Cards/")+file;
        IllustratedUi::Image(ImGui::GetWindowDrawList(),path.c_str(),p,ImVec2(p.x+size.x,p.y+size.y),IM_COL32_WHITE,true,gray);
    }
    void DrawBallDetailPopup(const PlayerBallData& ball,int)
    { ImGui::TextWrapped("%s\n%s",PlayerBallText::GetName(ball.definitionId),PlayerBallText::GetDescription(ball.definitionId)); }
}
int main(int argc,char** argv)
{
    if(argc!=2) { std::cerr<<"Usage: capture_lounge_services OUTPUT_DIRECTORY (run from DX22_01_plane)\n"; return 2; }
    ImGui::CreateContext(); auto& io=ImGui::GetIO();
    io.IniFilename=nullptr; io.DisplaySize={1672,941}; io.DeltaTime=1.0f/60;
    io.Fonts->AddFontFromFileTTF("assets/fonts/BIZUDPGothic-Regular.ttf",40,nullptr,io.Fonts->GetGlyphRangesJapanese());
    unsigned char* pixels; int w,h; io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
    Texture atlas; atlas.w=w;atlas.h=h;atlas.rgba.assign(pixels,pixels+w*h*4); textures[1]=std::move(atlas); io.Fonts->SetTexID(1);
    PlayerBallData balls[6];
    const char* names[]={"player_heavy","player_pierce","player_anchor"};
    for(int i=0;i<6;++i) {balls[i].definitionId=names[i%3]; balls[i].category=static_cast<BallCategory>(i%3==0?1:i%3==1?2:4);}
    LoungeServiceUi::ShopModel shop; shop.money=120; shop.balls={&balls[0],&balls[1],&balls[2]};
    shop.relics={&RelicCatalog[0],&RelicCatalog[1],&RelicCatalog[2]}; shop.relicOwned={false,false,true};
    for(auto& ball:balls) shop.deck.push_back(&ball);
    LoungeServiceUi::RestModel rest; rest.hp=28;rest.maxHp=45;rest.money=120;rest.healAmount=12;rest.healPercent=25;rest.canHeal=true;rest.availableBenefit=true;rest.deck=shop.deck;
    LoungeServiceUi::State state;
    for(const std::string scene:{"shop","rest","rest_used"})
    {
        rest.used=scene=="rest_used";
        for(int frame=0;frame<2;++frame)
        { ImGui::NewFrame(); if(scene=="shop") LoungeServiceUi::DrawShop(shop,state); else LoungeServiceUi::DrawRest(rest,state); ImGui::Render(); }
        Capture(std::filesystem::path(argv[1])/(scene+".ppm"));
    }
    ImGui::DestroyContext();
    std::cout<<"Captured production lounge UI using CPU rasterization; no DX11 gameplay was started.\n";
}

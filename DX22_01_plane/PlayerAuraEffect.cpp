#include "PlayerAuraEffect.h"
#include "imgui/imgui.h"

void PlayerAuraEffect::Draw(Position current, float projectedRadius, Project project) const
{
    if (!m_Active || project==nullptr || !std::isfinite(projectedRadius) || projectedRadius<=0) return;
    ImVec2 center,follow;
    if (!project(current,center)) return;
    if (!project(m_Follow,follow)) follow=center;
    auto* draw=ImGui::GetBackgroundDrawList(ImGui::GetMainViewport());
    const float radius=(std::max)(3.0f,projectedRadius);
    const float followDistance=std::hypot(follow.x-center.x,follow.y-center.y);
    if (followDistance>radius*0.8f)
    {
        const float scale=radius*0.8f/followDistance;
        follow={center.x+(follow.x-center.x)*scale,center.y+(follow.y-center.y)*scale};
    }
    const float pulse=1.0f+0.035f*std::sin(m_Phase);
    auto color=[&](float alpha)
    {
        return IM_COL32(static_cast<int>(m_Color[0]*255),static_cast<int>(m_Color[1]*255),
            static_cast<int>(m_Color[2]*255),static_cast<int>(alpha));
    };
    // Leave the cue-ball silhouette clear, including where the lagging halo
    // overlaps it. HUD windows render above this background layer.
    auto ring=[&](ImVec2 origin,float r,float alpha,float width)
    {
        constexpr int segments=48;
        for (int i=0;i<segments;++i)
        {
            const float a=6.283185307f*i/segments,b=6.283185307f*(i+1)/segments;
            ImVec2 p{origin.x+std::cos(a)*r,origin.y+std::sin(a)*r};
            ImVec2 q{origin.x+std::cos(b)*r,origin.y+std::sin(b)*r};
            const float dx=(p.x+q.x)*0.5f-center.x,dy=(p.y+q.y)*0.5f-center.y;
            const float clearRadius=radius*1.04f+width*0.5f;
            if (dx*dx+dy*dy>=clearRadius*clearRadius)
                draw->AddLine(p,q,color(alpha),width);
        }
    };
    const float dx=follow.x-center.x,dy=follow.y-center.y;
    if (dx*dx+dy*dy>0.25f)
    {
        for (int layer=0;layer<4;++layer)
            ring(follow,radius*(1.16f+layer*0.12f),12.0f-layer*2.0f,(std::max)(1.0f,radius*0.14f));
    }
    for (int layer=5;layer>=0;--layer)
        ring(center,radius*(1.12f+layer*0.085f)*pulse,28.0f-layer*3.5f,
            (std::max)(1.0f,radius*0.10f));
    ring(center,radius*1.10f,130.0f,(std::max)(1.2f,radius*0.045f));
}

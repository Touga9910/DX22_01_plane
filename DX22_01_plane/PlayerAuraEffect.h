#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

struct ImVec2;

// Presentation-only state: independent of physics, ball models and shot rules.
class PlayerAuraEffect final
{
public:
    struct Position { float x=0, y=0, z=0; };
    using Color = std::array<float,3>;
    using Project = bool (*)(const Position&, ImVec2&);

    void Reset() { m_Active=false; m_Phase=0; }
    bool IsActive() const { return m_Active; }
    Position FollowPosition() const { return m_Follow; }
    float Phase() const { return m_Phase; }

    void Update(Position target, float radius, Color color,
        std::uintptr_t identity, float deltaSeconds)
    {
        if (!std::isfinite(target.x) || !std::isfinite(target.y) ||
            !std::isfinite(target.z) || !std::isfinite(radius) || radius<=0)
        {
            Reset();
            return;
        }
        const float dt=std::isfinite(deltaSeconds) ? std::clamp(deltaSeconds,0.0f,0.1f) : 0;
        Position delta{target.x-m_Follow.x,target.y-m_Follow.y,target.z-m_Follow.z};
        const float distance=std::sqrt(delta.x*delta.x+delta.y*delta.y+delta.z*delta.z);
        if (!m_Active || identity!=m_Identity || color!=m_Color || distance>radius*8)
        {
            m_Follow=target;
            m_Phase=0;
        }
        else if (dt>0)
        {
            const float blend=1-std::exp(-14.0f*dt);
            m_Follow.x+=delta.x*blend;
            m_Follow.y+=delta.y*blend;
            m_Follow.z+=delta.z*blend;
            // The glow stays close even during a very fast shot or a wall hit.
            const float lag=distance*(1-blend);
            const float maxLag=radius*0.8f;
            if (lag>maxLag)
            {
                const float scale=maxLag/distance;
                m_Follow={target.x-delta.x*scale,target.y-delta.y*scale,target.z-delta.z*scale};
            }
        }
        m_Color=color;
        m_Identity=identity;
        m_Active=true;
        m_Phase=std::fmod(m_Phase+dt*1.8f,6.283185307f);
    }

    // The main halo uses the current render position, even when the tracking
    // update occurred before this frame's fixed physics ticks.
    void Draw(Position current, float projectedRadius, Project project) const;

private:
    bool m_Active=false;
    Position m_Follow{};
    Color m_Color{0.25f,0.63f,1.0f};
    std::uintptr_t m_Identity=0;
    float m_Phase=0;
};

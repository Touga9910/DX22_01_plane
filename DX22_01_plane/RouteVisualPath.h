#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace RouteVisualPath
{
    struct Point { float x=0,y=0; };
    struct Segment { Point from,to; };
    /// <summary>
    /// 入力整数のビットを混ぜ、ルートの見た目の生成に使う決定的な値を返す。
    /// </summary>
    inline std::uint32_t Mix(std::uint32_t n)
    { n^=n>>16;n*=0x7feb352du;n^=n>>15;n*=0x846ca68bu;return n^(n>>16); }
    /// <summary>
    /// シードとノード番号から、2点間に描くルートの曲線を生成する。
    /// </summary>
    inline std::array<Point,65> Curve(std::uint32_t seed,int from,int to,Point a,Point b)
    {
        const auto hash=Mix(seed^Mix(static_cast<std::uint32_t>(from))^Mix(static_cast<std::uint32_t>(to)+0x9e3779b9u));
        const float dx=b.x-a.x,dy=b.y-a.y;
        const float bend=(hash&1?1.0f:-1.0f)*(0.13f+0.08f*((hash>>1)&255)/255.0f);
        const float bend2=bend*(0.4f+0.6f*((hash>>9)&255)/255.0f);
        Point c{a.x+dx/3-dy*bend,a.y+dy/3+dx*bend};
        Point d{a.x+dx*2/3-dy*bend2,a.y+dy*2/3+dx*bend2};
        std::array<Point,65> result{};
        for(int i=0;i<=64;++i)
        {
            const float t=i/64.0f,u=1-t;
            result[i]={u*u*u*a.x+3*u*u*t*c.x+3*u*t*t*d.x+t*t*t*b.x,
                u*u*u*a.y+3*u*u*t*c.y+3*u*t*t*d.y+t*t*t*b.y};
        }
        return result;
    }
    /// <summary>
    /// 曲線に沿って一定間隔で区切り、破線として描く線分を返す。
    /// </summary>
    inline std::vector<Segment> Dashes(const std::array<Point,65>& curve,float scale)
    {
        std::vector<Segment> result;
        const float dash=11*(std::max)(0.01f,scale),period=19*(std::max)(0.01f,scale);
        float distance=0;
        for(int i=1;i<65;++i)
        {
            const auto a=curve[i-1],b=curve[i];
            const float length=std::hypot(b.x-a.x,b.y-a.y);
            float used=0;
            while(used<length-0.00001f)
            {
                const float phase=std::fmod(distance,period);
                const bool visible=phase<dash;
                const float step=(std::min)(length-used,(visible?dash:period)-phase+0.00001f);
                if(visible) result.push_back({{a.x+(b.x-a.x)*used/length,a.y+(b.y-a.y)*used/length},
                    {a.x+(b.x-a.x)*(used+step)/length,a.y+(b.y-a.y)*(used+step)/length}});
                used+=step;distance+=step;
            }
        }
        return result;
    }
}

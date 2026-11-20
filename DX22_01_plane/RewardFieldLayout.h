#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace RewardFieldLayout
{
    constexpr float CanvasWidth = 1448.0f;
    constexpr float CanvasHeight = 970.0f;
    struct Point { float x=0,y=0; };
    struct Bounds { float left=0,top=0,width=0,height=0; bool valid=false; };
    inline bool Inside(const std::array<Point,4>& quad, Point p)
    {
        bool positive=false,negative=false;
        for (int i=0;i<4;++i)
        {
            const auto a=quad[i],b=quad[(i+1)%4];
            const float cross=(b.x-a.x)*(p.y-a.y)-(b.y-a.y)*(p.x-a.x);
            positive|=cross>0.01f; negative|=cross<-0.01f;
        }
        return !(positive&&negative);
    }
    inline Bounds Fit(const std::array<Point,4>& quad, float width=CanvasWidth, float height=CanvasHeight)
    {
        Point center{};
        float minX=quad[0].x,maxX=minX,minY=quad[0].y,maxY=minY;
        for (const auto p:quad)
        {
            if (!std::isfinite(p.x)||!std::isfinite(p.y)) return {};
            center.x+=p.x*0.25f; center.y+=p.y*0.25f;
            minX=(std::min)(minX,p.x);maxX=(std::max)(maxX,p.x);
            minY=(std::min)(minY,p.y);maxY=(std::max)(maxY,p.y);
        }
        float low=0,high=(std::min)((maxX-minX)/width,(maxY-minY)/height);
        for (int i=0;i<28;++i)
        {
            const float scale=(low+high)*0.5f;
            bool inside=true;
            for(float x:{-1.0f,1.0f}) for(float y:{-1.0f,1.0f})
                inside&=Inside(quad,{center.x+x*width*scale*.5f,center.y+y*height*scale*.5f});
            if(inside)low=scale;else high=scale;
        }
        low*=0.98f; // retain a little breathing room inside the projected cloth
        return {center.x-width*low*.5f,center.y-height*low*.5f,width*low,height*low,low>0.001f};
    }
}

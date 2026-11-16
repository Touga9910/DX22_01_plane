#pragma once

#include "TableConfig.h"
#include <algorithm>
#include <cmath>
#include <vector>

// Render-only geometry in table-local units. No collision or gameplay state.
// Plain values allow the exact production mesh to be checked without DirectX.
namespace TableVisualMesh
{
    struct Point { float x, y, z; };
    struct Tint { float r, g, b; };
    struct Vertex { Point position, normal; Tint color; };
    using Mesh = std::vector<Vertex>; // independent triangles
    constexpr float Pi = 3.14159265358979323846f;
    constexpr float HalfWidth = TableConfig::GetFieldWidth() * 0.5f;
    constexpr float HalfDepth = TableConfig::GetFieldDepth() * 0.5f;
    constexpr float OuterWidth = TableConfig::TABLE_OUTER_WIDTH * 0.5f;
    constexpr float OuterDepth = TableConfig::TABLE_OUTER_DEPTH * 0.5f;
    constexpr float CushionDepth = 1.6f;
    constexpr float RailHeight = 0.82f;
    constexpr int CurveSteps = 32;
    constexpr Tint Wood{0.29f, 0.14f, 0.065f};
    constexpr Tint WoodEdge{0.16f, 0.068f, 0.027f};
    constexpr Tint Cushion{0.045f, 0.36f, 0.23f};
    constexpr Tint Rubber{0.035f, 0.043f, 0.038f};
    constexpr Tint Sights{0.86f, 0.80f, 0.62f};

    inline Point Sub(Point a, Point b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
    inline Point Cross(Point a, Point b)
    { return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x}; }
    inline float Dot(Point a, Point b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
    inline void Triangle(Mesh& mesh, Point a, Point b, Point c, Tint color,
        Point facing = {0, 1, 0})
    {
        Point normal = Cross(Sub(b,a), Sub(c,a));
        const float length = std::sqrt(Dot(normal,normal));
        if (length < 0.000001f) return;
        if (Dot(normal, facing) < 0) { std::swap(b,c); normal = {-normal.x,-normal.y,-normal.z}; }
        normal = {normal.x/length,normal.y/length,normal.z/length};
        for (Point p : {a,b,c}) mesh.push_back({p,normal,color});
    }
    inline void Quad(Mesh& mesh, Point a, Point b, Point c, Point d, Tint color,
        Point facing = {0, 1, 0})
    {
        Triangle(mesh,a,b,c,color,facing);
        Triangle(mesh,c,b,d,color,facing);
    }
    inline float Radius(float x)
    { return std::abs(x) < 0.001f ? TableConfig::SIDE_POCKET_MOUTH_HALF_WIDTH : TableConfig::CORNER_POCKET_MOUTH_HALF_WIDTH; }
    inline float Arc(float radius, float offset)
    { return std::sqrt((std::max)(0.0f, radius*radius-offset*offset)); }

    inline std::vector<float> Samples(float extent, bool alongX)
    {
        std::vector<float> values{-extent,extent};
        for (float center : {- (alongX ? HalfWidth : HalfDepth), 0.0f, alongX ? HalfWidth : HalfDepth})
        {
            if (!alongX && center == 0) continue;
            const float radius = alongX ? Radius(center) : Radius(HalfWidth);
            for (int step = -CurveSteps; step <= CurveSteps; ++step)
            {
                const float p = center + radius * std::sin(Pi*0.5f*step/CurveSteps);
                if (p > -extent && p < extent) values.push_back(p);
            }
            // Exact intersections with the inner wooden rail avoid tiny
            // triangular bridges across a pocket at the edge of a sample.
            for (float sign : {-1.0f,1.0f})
            {
                const float p = center + sign*Arc(radius,CushionDepth);
                if (p > -extent && p < extent) values.push_back(p);
            }
        }
        std::sort(values.begin(),values.end());
        values.erase(std::unique(values.begin(),values.end()),values.end());
        return values;
    }
    inline float CutDepth(float coordinate, bool alongX)
    {
        float depth = 0;
        for (float center : {-(alongX ? HalfWidth : HalfDepth), 0.0f, alongX ? HalfWidth : HalfDepth})
        {
            if (!alongX && center == 0) continue;
            const float radius = alongX ? Radius(center) : Radius(HalfWidth);
            if (std::abs(coordinate-center) <= radius)
                depth = (std::max)(depth,Arc(radius,coordinate-center));
        }
        return depth;
    }

    inline Mesh BuildFelt()
    {
        Mesh mesh;
        const auto samples = Samples(HalfWidth,true);
        for (std::size_t i=1; i<samples.size(); ++i)
        {
            const float x0=samples[i-1], x1=samples[i];
            auto mouthDepth=[](float x)
            {
                // Corner mouths cross the playing surface diagonally, while
                // the side mouths keep a rounded shelf into the throat.
                const float corner=(std::max)(0.0f,Radius(HalfWidth)-(HalfWidth-std::abs(x)));
                const float side=std::abs(x)<=Radius(0) ? Arc(Radius(0),x) : 0;
                return (std::max)(corner,side);
            };
            const float z0=HalfDepth-mouthDepth(x0), z1=HalfDepth-mouthDepth(x1);
            Quad(mesh,{x0,0,z0},{x1,0,z1},{x0,0,-z0},{x1,0,-z1},{1,1,1});
        }
        return mesh;
    }

    inline void BuildWood(Mesh& mesh)
    {
        for (bool alongX : {true,false})
        {
            const float extent = alongX ? OuterWidth : HalfDepth+CushionDepth;
            const float field = alongX ? HalfDepth : HalfWidth;
            const float outer = alongX ? OuterDepth : OuterWidth;
            const auto samples=Samples(extent,alongX);
            for (float side : {-1.0f,1.0f})
            {
                auto p=[&](float u,float v,float y) -> Point
                { return alongX ? Point{u,y,side*v} : Point{side*v,y,u}; };
                for (std::size_t i=1;i<samples.size();++i)
                {
                    float a=samples[i-1], b=samples[i];
                    float v0=field+(std::max)(CushionDepth,CutDepth(a,alongX));
                    float v1=field+(std::max)(CushionDepth,CutDepth(b,alongX));
                    Quad(mesh,p(a,outer,RailHeight),p(b,outer,RailHeight),
                        p(a,v0,RailHeight),p(b,v1,RailHeight),Wood);
                }
            }
        }
        // Bevelled cabinet. All edges stay within the existing outer bounds.
        for (bool alongX : {true,false})
        {
            const float extent=alongX ? OuterWidth : OuterDepth;
            const float outer=alongX ? OuterDepth : OuterWidth;
            for (float side : {-1.0f,1.0f})
            {
                auto p=[&](float u,float v,float y) -> Point
                { return alongX ? Point{u,y,side*v} : Point{side*v,y,u}; };
                const Point facing=alongX ? Point{0,0,side} : Point{side,0,0};
                Quad(mesh,p(-extent,outer,RailHeight),p(extent,outer,RailHeight),
                    p(-extent,outer,-0.1f),p(extent,outer,-0.1f),WoodEdge,facing);
                Quad(mesh,p(-extent,outer,-0.1f),p(extent,outer,-0.1f),
                    p(-extent+0.5f,outer-0.5f,-3.6f),p(extent-0.5f,outer-0.5f,-3.6f),Wood,facing);
            }
        }
    }

    inline void BuildCushions(Mesh& mesh)
    {
        for (bool alongX : {true,false})
        {
            const float length=alongX ? HalfWidth : HalfDepth;
            const float field=alongX ? HalfDepth : HalfWidth;
            const int count=alongX ? 2 : 1;
            for (float side : {-1.0f,1.0f}) for (int part=0;part<count;++part)
            {
                const float start=-length+part*length*2/count;
                const float end=start+length*2/count;
                const float ra=alongX ? Radius(start) : Radius(HalfWidth);
                const float rb=alongX ? Radius(end) : Radius(HalfWidth);
                auto p=[&](float u,float depth,float y) -> Point
                { return alongX ? Point{u,y,side*(field+depth)} : Point{side*(field+depth),y,u}; };
                const Point inward=alongX ? Point{0,0,-side} : Point{-side,0,0};
                const Point a=p(start+ra,0,0.12f), b=p(end-rb,0,0.12f);
                const Point c=p(start+Arc(ra,0.45f),0.45f,0.72f);
                const Point d=p(end-Arc(rb,0.45f),0.45f,0.72f);
                const Point e=p(start+Arc(ra,CushionDepth),CushionDepth,RailHeight);
                const Point f=p(end-Arc(rb,CushionDepth),CushionDepth,RailHeight);
                Quad(mesh,a,b,c,d,Cushion);
                Quad(mesh,c,d,e,f,Cushion);
                Quad(mesh,p(start+ra,0,-0.08f),p(end-rb,0,-0.08f),a,b,{0.025f,0.21f,0.13f},inward);
                // Cut jaw faces follow the mouth into the pocket liner.
                for (bool atEnd : {false,true})
                {
                    const Point nose=atEnd?b:a, shoulder=atEnd?d:c, back=atEnd?f:e;
                    const Point jawFacing=alongX ? Point{atEnd?1.0f:-1.0f,0,0} : Point{0,0,atEnd?1.0f:-1.0f};
                    Point lowN=nose, lowS=shoulder, lowB=back;
                    lowN.y=lowS.y=lowB.y=-0.08f;
                    Quad(mesh,nose,shoulder,lowN,lowS,Rubber,jawFacing);
                    Quad(mesh,shoulder,back,lowS,lowB,Rubber,jawFacing);
                }
            }
        }
    }

    inline void BuildPockets(Mesh& mesh)
    {
        for (float x : {-HalfWidth,0.0f,HalfWidth}) for (float z : {-HalfDepth,HalfDepth})
        {
            const float radius=Radius(x);
            for (int i=0;i<CurveSteps*2;++i)
            {
                const float a=2*Pi*i/(CurveSteps*2), b=2*Pi*(i+1)/(CurveSteps*2);
                auto contourRadius=[&](float angle,float r)
                {
                    const float c=std::cos(angle),s=std::sin(angle);
                    return x!=0 && c*x<0 && s*z<0 ? r/(std::abs(c)+std::abs(s)) : r;
                };
                auto lip=[&](float angle,float r) -> Point
                {
                    r=contourRadius(angle,r);
                    const float px=x+std::cos(angle)*r, pz=z+std::sin(angle)*r;
                    const float outside=(std::max)((std::max)(0.0f,std::abs(px)-HalfWidth),(std::max)(0.0f,std::abs(pz)-HalfDepth));
                    return {px,0.025f+(RailHeight-0.025f)*(std::min)(1.0f,outside/CushionDepth),pz};
                };
                const Point outer0=lip(a,radius), outer1=lip(b,radius);
                const Point inner0=lip(a,radius-0.22f), inner1=lip(b,radius-0.22f);
                Quad(mesh,outer0,outer1,inner0,inner1,Rubber);
                Point bottom0{x+std::cos(a)*contourRadius(a,radius-0.5f),-2.7f,z+std::sin(a)*contourRadius(a,radius-0.5f)};
                Point bottom1{x+std::cos(b)*contourRadius(b,radius-0.5f),-2.7f,z+std::sin(b)*contourRadius(b,radius-0.5f)};
                Quad(mesh,inner0,inner1,bottom0,bottom1,{0.015f,0.018f,0.016f},{-std::cos(a),0,-std::sin(a)});
                Triangle(mesh,{x,-2.7f,z},bottom0,bottom1,{0.003f,0.004f,0.003f});
            }
        }
    }

    inline void BuildSights(Mesh& mesh)
    {
        for (bool alongX : {true,false}) for (float side : {-1.0f,1.0f})
        {
            const int divisions=alongX ? 8 : 4;
            const float extent=alongX ? HalfWidth : HalfDepth;
            const float v=(alongX ? HalfDepth : HalfWidth)+2.6f;
            auto p=[&](float u,float offset) -> Point
            { return alongX ? Point{u,RailHeight+0.015f,side*(v+offset)} : Point{side*(v+offset),RailHeight+0.015f,u}; };
            for (int i=1;i<divisions;++i)
            {
                if (alongX && i==divisions/2) continue; // side pocket
                const float u=-extent+2*extent*i/divisions;
                Triangle(mesh,p(u-0.32f,0),p(u,0.20f),p(u+0.32f,0),Sights);
                Triangle(mesh,p(u-0.32f,0),p(u+0.32f,0),p(u,-0.20f),Sights);
            }
        }
    }
    inline Mesh BuildFrame()
    {
        Mesh mesh;
        BuildWood(mesh);
        BuildCushions(mesh);
        BuildPockets(mesh);
        BuildSights(mesh);
        return mesh;
    }
}

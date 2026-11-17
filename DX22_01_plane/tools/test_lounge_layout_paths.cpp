#include "../RewardFieldLayout.h"
#include "../RouteVisualPath.h"
#include "../RunMap.h"
#include <cassert>
#include <iostream>
int main()
{
    using namespace RewardFieldLayout;
    for (const auto quad : {std::array<Point,4>{{{80,120},{1500,120},{1500,820},{80,820}}},
        std::array<Point,4>{{{200,180},{1400,120},{1500,800},{100,860}}}})
    {
        const auto b=Fit(quad);
        assert(b.valid && b.width>0 && b.height>0);
        for(float x:{b.left,b.left+b.width})for(float y:{b.top,b.top+b.height})assert(Inside(quad,{x,y}));
        // Entire logical canvas includes every reward item from header to skip.
        assert(std::abs(b.width/b.height-1448.0f/1086)<0.0001f);
    }
    assert(!Fit({}).valid);
    auto first=RouteVisualPath::Curve(123,4,7,{0,0},{0,115});
    bool changed=false;
    for(unsigned seed=0;seed<100;++seed)
    {
        const auto a=RouteVisualPath::Curve(seed,4,7,{0,0},{350,115});
        const auto b=RouteVisualPath::Curve(seed,4,7,{0,0},{350,115});
        const auto other=RouteVisualPath::Curve(seed+1,4,7,{0,0},{350,115});
        assert(a.front().x==0 && a.back().x==350 && a.back().y==115);
        for(int i=0;i<65;++i){assert(a[i].x==b[i].x && a[i].y==b[i].y);changed|=a[i].x!=other[i].x;}
        assert(std::abs(a[32].x*115-a[32].y*350)>1); // non-straight
        const auto dashes=RouteVisualPath::Dashes(a,1);
        assert(!dashes.empty());
        bool gap=false;
        for(std::size_t i=0;i<dashes.size();++i)
        {
            const auto d=dashes[i];
            assert(std::hypot(d.to.x-d.from.x,d.to.y-d.from.y)<=11.001f);
            if(i)gap|=std::hypot(d.from.x-dashes[i-1].to.x,d.from.y-dashes[i-1].to.y)>1;
        }
        assert(gap);
    }
    assert(changed && std::abs(first[32].x)>1);
    RunMap map;
    map.Generate(87123);
    const auto restored=RunMap::Restore(map.Save());
    assert(map.Seed()==restored.Seed());
    const auto& node=map.Nodes().front();
    for(int target:node.next)
    {
        const auto a=RouteVisualPath::Curve(map.Seed(),node.id,target,{0,0},{350,115});
        const auto b=RouteVisualPath::Curve(restored.Seed(),node.id,target,{0,0},{350,115});
        for(int i=0;i<65;++i)assert(a[i].x==b[i].x && a[i].y==b[i].y);
    }
    std::cout<<"Projected field fit, deterministic seeded curves and dashed segments passed\n";
}

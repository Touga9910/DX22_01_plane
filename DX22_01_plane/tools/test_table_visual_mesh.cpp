#include "../TableVisualMesh.h"
#include <cassert>
#include <iostream>
#include <string>

using namespace TableVisualMesh;

bool Covers(const Mesh& mesh, float x, float z, float minimumY)
{
    for (std::size_t i=0; i<mesh.size(); i+=3)
    {
        const Point a=mesh[i].position,b=mesh[i+1].position,c=mesh[i+2].position;
        const float det=(b.z-c.z)*(a.x-c.x)+(c.x-b.x)*(a.z-c.z);
        if (std::abs(det)<0.000001f) continue;
        const float u=((b.z-c.z)*(x-c.x)+(c.x-b.x)*(z-c.z))/det;
        const float v=((c.z-a.z)*(x-c.x)+(a.x-c.x)*(z-c.z))/det;
        const float w=1-u-v;
        if (u>=-0.00001f && v>=-0.00001f && w>=-0.00001f &&
            u*a.y+v*b.y+w*c.y>minimumY) return true;
    }
    return false;
}

void Check(const Mesh& mesh)
{
    assert(!mesh.empty() && mesh.size()%3==0);
    for (std::size_t i=0;i<mesh.size();i+=3)
    {
        const Point cross=Cross(Sub(mesh[i+1].position,mesh[i].position),Sub(mesh[i+2].position,mesh[i].position));
        assert(Dot(cross,mesh[i].normal)>0.000001f); // front faces survive back-face culling
        for (std::size_t j=i;j<i+3;++j)
        {
            const auto& v=mesh[j];
            assert(std::isfinite(v.position.x) && std::isfinite(v.position.y) && std::isfinite(v.position.z));
            assert(std::abs(v.position.x)<=OuterWidth+0.0001f);
            assert(std::abs(v.position.z)<=OuterDepth+0.0001f);
            assert(std::abs(Dot(v.normal,v.normal)-1)<0.0001f);
        }
    }
}

int main(int argc, char** argv)
{
    static_assert(TableConfig::GetFieldWidth()==144 && TableConfig::GetFieldDepth()==72);
    static_assert(TableConfig::TABLE_OUTER_WIDTH==152 && TableConfig::TABLE_OUTER_DEPTH==80);
    const auto frame=BuildFrame(),felt=BuildFelt();
    Check(frame); Check(felt);
    assert(frame.size()<30000); // fixed mesh, built once per table
    assert(Covers(felt,0,0,-0.01f));
    assert(Covers(felt,HalfWidth,0,-0.01f));
    assert(Covers(felt,HalfWidth*0.5f,HalfDepth,-0.01f));
    assert(Covers(frame,0,OuterDepth-0.1f,0.5f));
    for (float x : {-HalfWidth,0.0f,HalfWidth}) for (float z : {-HalfDepth,HalfDepth})
    {
        assert(!Covers(felt,x,z,-0.01f));
        assert(!Covers(frame,x,z,-2.0f)); // open above the sunken pocket bottom
        assert(Covers(frame,x,z,-2.8f));
        for (int direction=0; direction<16; ++direction)
        {
            const float angle=2*Pi*direction/16;
            assert(!Covers(frame,x+1.2f*std::cos(angle),z+1.2f*std::sin(angle),-2.0f));
        }
        const float inwardX=x==0 ? 0 : (x>0 ? -1.0f : 1.0f);
        const float inwardZ=z>0 ? -1.0f : 1.0f;
        assert(!Covers(felt,x+inwardX,z+inwardZ,-0.01f));
        if (x!=0)
            assert(Covers(felt,x+2*inwardX,z+2*inwardZ,-0.01f)); // diagonal corner jaw, not a circular decal
    }
    if (argc>1 && std::string(argv[1])=="--dump")
    {
        std::cout << '[';
        bool first=true;
        for (const auto* mesh : {&felt,&frame}) for (const auto& v : *mesh)
        {
            if (!first) std::cout << ',';
            first=false;
            std::cout << '[' << v.position.x << ',' << v.position.y << ',' << v.position.z << ','
                << v.color.r << ',' << v.color.g << ',' << v.color.b << ']';
        }
        std::cout << "]\n";
    }
    else std::cout << "Table visual mesh passed: " << frame.size()/3 << " frame triangles, "
        << felt.size()/3 << " felt triangles\n";
}

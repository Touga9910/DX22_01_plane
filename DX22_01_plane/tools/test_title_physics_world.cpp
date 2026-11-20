#include "../TitlePhysicsWorld.h"
#include "../ShotPowerRules.h"
#include <cassert>
#include <cmath>
#include <iostream>
TitlePhysicsWorld OneBall()
{
    TitlePhysicsWorld w;w.active.fill(false);w.active[0]=true;
    for(int i=0;i<16;++i)
    {
        auto& b=w.balls[i];b.id=i+1;b.status.radius=2.4f;b.status.mass=2;b.status.restitution=.8f;b.status.friction=.02f;
        b.position={-44,TableConfig::FIELD_HEIGHT,0};b.player=i==0;b.enemy=i!=0;
    }
    return w;
}
int main()
{
    auto movement=OneBall();movement.balls[0].velocity={ShotPowerRules::AtFraction(.8f),0,0};
    assert(!movement.Tick().limitReached);
    assert(std::abs(movement.balls[0].velocity.x-6.38f)<.0001f);
    assert(std::abs(movement.balls[0].position.x-(-44+6.38f))<.0001f); // same units as gameplay, not seconds
    auto collision=OneBall();collision.active[1]=true;collision.balls[1].position={-30,TableConfig::FIELD_HEIGHT,0};
    collision.balls[0].velocity={80,0,0};
    assert(!collision.Tick().limitReached);
    assert(collision.hit&&collision.balls[1].velocity.x>0);
    auto pocket=OneBall();pocket.balls[0].position=TableConfig::GetPocketCenters()[1];pocket.Tick();assert(!pocket.active[0]);
    auto wall=OneBall();wall.balls[0].position={-25,TableConfig::FIELD_HEIGHT,33.4f};wall.balls[0].velocity={0,0,2};wall.Tick();
    assert(wall.active[0]&&wall.balls[0].velocity.z<0);
    std::cout<<"PASS: live tick units/friction, CCD transfer, real cushion walls, pocket removal\n";
}

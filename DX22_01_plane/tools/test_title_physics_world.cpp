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
    auto spread=OneBall();spread.active.fill(true);spread.breakFanEnabled=true;
    const float spacing=4.82f;int slot=1;
    for(int row=0;row<5;++row)for(int col=0;col<=row;++col)
    {
        auto& b=spread.balls[slot++];b.status.mass=1;b.status.friction=.013f;
        b.position={19+row*spacing*.87f,TableConfig::FIELD_HEIGHT,(col-row*.5f)*spacing};
    }
    spread.balls[0].position.z=-12.0f;
    auto breakDirection=spread.balls[1].position-spread.balls[0].position;
    breakDirection.Normalize();
    spread.balls[0].velocity=breakDirection*ShotPowerRules::AtFraction(.95f);
    bool returnsTowardCue=false;float backwardTravel=0;
    for(int tick=0;tick<180;++tick)
    {
        const auto before=spread.balls;
        assert(!spread.Tick().limitReached);
        for(int flank:{2,3})
        {
            if(spread.balls[flank].velocity.x<-.2f)returnsTowardCue=true;
            backwardTravel+=(std::max)(0.0f,before[flank].position.x-spread.balls[flank].position.x);
        }
    }
    assert(spread.hit&&spread.breakFanApplied&&returnsTowardCue&&backwardTravel>10);
    assert(!collision.breakFanApplied); // Accent remains opt-in and title-local.
    std::cout<<"Title flank backward travel: "<<backwardTravel<<"\n";
    std::cout<<"PASS: live tick units/friction, CCD transfer, real cushion walls, pocket removal\n";
}

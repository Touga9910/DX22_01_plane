#include "../ContinuousBallStepper.h"
#include <cassert>
#include <iostream>
#include <limits>
#include <crtdbg.h>

using namespace BallPhysicsRules;

struct World
{
    std::vector<Body> balls;
    std::vector<Collision::Segment> walls;
    std::vector<Collision::Sphere> pockets;
    std::vector<Vector3> starts;
    std::vector<bool> active;
    Vector3 interior = Vector3::Zero;
    int hits = 0, wallHits = 0, pocketHits = 0;
    bool abort = false, abortOnPocket = false;
    std::size_t Count() const { return balls.size(); }
    bool IsActive(std::size_t i) const { return active[i]; }
    bool ShouldContinue() const { return !abort; }
    const auto& PhysicsBody(std::size_t i) const { return balls[i]; }
    const auto& Walls() const { return walls; }
    const auto& PocketSpheres() const { return pockets; }
    void BeginSubstep() { starts.clear(); for (auto& ball : balls) starts.push_back(ball.position); }
    void Move(std::size_t i, float time) { balls[i].position += balls[i].velocity * time; }
    bool Pocket(std::size_t i, const Vector3& start)
    {
        for (const auto& p : pockets)
            if (PocketHit(start, balls[i].position, balls[i].status.radius, p))
            {
                active[i] = false; balls[i].velocity = Vector3::Zero; ++pocketHits;
                if (abortOnPocket) abort = true;
                return true;
            }
        return false;
    }
    void Environment(std::size_t i, const Vector3& start)
    {
        if (!active[i] || Pocket(i,start)) return;
        for (const auto& wall : walls) if (Wall(balls[i],wall,interior)) ++wallHits;
        Pocket(i,start);
    }
    void ResolveEnvironment(std::size_t i) { Environment(i, starts[i]); }
    void ResolvePair(std::size_t i, std::size_t j)
    {
        const Vector3 ap=balls[i].position, bp=balls[j].position, av=balls[i].velocity, bv=balls[j].velocity;
        if (Pair(balls[i],balls[j]) && !abort)
        {
            ++hits;
            Vector3 acceleration;
            StopAnchor(balls[i], acceleration); StopAnchor(balls[j], acceleration);
        }
        if(active[i] && (balls[i].position!=ap || balls[i].velocity!=av)) Environment(i,ap);
        if(active[j] && (balls[j].position!=bp || balls[j].velocity!=bv)) Environment(j,bp);
    }
    void Add(Vector3 position, Vector3 velocity, bool player=false)
    {
        Body b; b.id=balls.size()+1; b.position=position; b.velocity=velocity;
        b.status.radius=1; b.status.mass=1; b.status.restitution=1; b.player=player; b.enemy=!player;
        balls.push_back(b); active.push_back(true);
    }
};

bool Near(float a, float b, float tolerance=0.001f) { return std::abs(a-b)<tolerance; }
void Complete(const ContinuousBallStepper::Result& r) { assert(!r.limitReached && std::abs(r.advancedFraction-1)<1e-9); }

int main()
{
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    using namespace BallCcdGeometry;
    assert(std::abs(SphereTime(Vector3(-10,0,0),Vector3(100,0,0),2,1)-0.08)<1e-12);
    assert(!std::isfinite(SphereTime(Vector3(-10,0,0),Vector3(-100,0,0),2,1)));
    assert(!std::isfinite(SphereTime(Vector3(2,0,0),Vector3(1,0,0),2,1)));
    const Collision::Segment vertical{Vector3(0,0,-10),Vector3(0,0,10)};
    assert(std::abs(WallTime(Vector3(-10,0,0),Vector3(100,0,0),1,vertical,1)-0.09)<1e-12);
    const Collision::Segment cap{Vector3(0,0,0),Vector3(0,0,10)};
    assert(std::abs(WallTime(Vector3(-5,0,-0.5f),Vector3(10,0,0),1,cap,1)-(5-std::sqrt(0.75))/10)<1e-9);
    assert(!std::isfinite(WallTime(Vector3(-5,0,-1.01f),Vector3(10,0,0),1,cap,1)));

    World fast;
    fast.Add(Vector3(-10,0,0),Vector3(1000,0,0),true);
    fast.Add(Vector3(10,0,0),Vector3(-1000,0,0));
    Complete(ContinuousBallStepper::Step(fast));
    assert(fast.hits==1 && fast.balls[0].velocity.x<0 && fast.balls[1].velocity.x>0);
    assert(Near(fast.balls[0].position.x,-992));

    World bounce;
    bounce.Add(Vector3(-5,0,0),Vector3(100,0,0),true);
    bounce.walls.push_back(vertical); bounce.interior=Vector3(-10,0,0);
    Complete(ContinuousBallStepper::Step(bounce));
    assert(bounce.wallHits==1 && Near(bounce.balls[0].position.x,-97));

    World endpoint;
    endpoint.Add(Vector3(-5,0,-0.5f),Vector3(10,0,0),true);
    endpoint.walls.push_back(cap); endpoint.interior=Vector3(-10,0,0);
    Complete(ContinuousBallStepper::Step(endpoint));
    assert(endpoint.wallHits==1 && endpoint.balls[0].velocity.x<0 && endpoint.balls[0].velocity.z<0);

    World tangent;
    tangent.Add(Vector3(-5,0,-1),Vector3(10,0,0),true); tangent.walls.push_back(cap);
    Complete(ContinuousBallStepper::Step(tangent));
    assert(tangent.wallHits==0 && Near(tangent.balls[0].position.x,5));

    World pairTangent;
    pairTangent.Add(Vector3(-5,0,-2),Vector3(10,0,0),true);
    pairTangent.Add(Vector3::Zero,Vector3::Zero);
    Complete(ContinuousBallStepper::Step(pairTangent));
    assert(pairTangent.hits==0 && Near(pairTangent.balls[0].position.x,5));

    World wallFirst;
    wallFirst.Add(Vector3(-5,0,0),Vector3(100,0,0),true);
    wallFirst.Add(Vector3(10,0,0),Vector3::Zero);
    wallFirst.walls.push_back(vertical); wallFirst.interior=Vector3(-10,0,0);
    Complete(ContinuousBallStepper::Step(wallFirst));
    assert(wallFirst.hits==0 && wallFirst.wallHits==1);

    World repeated;
    repeated.Add(Vector3::Zero,Vector3(40,0,0),true);
    repeated.walls={ {Vector3(-5,0,-10),Vector3(-5,0,10)}, {Vector3(5,0,-10),Vector3(5,0,10)} };
    Complete(ContinuousBallStepper::Step(repeated));
    assert(repeated.wallHits==5 && Near(repeated.balls[0].position.x,0));

    World chain;
    chain.Add(Vector3(-4,0,0),Vector3(10,0,0),true);
    chain.Add(Vector3(0,0,0),Vector3::Zero);
    chain.Add(Vector3(2,0,0),Vector3::Zero);
    Complete(ContinuousBallStepper::Step(chain));
    assert(chain.hits==2 && Near(chain.balls[2].velocity.x,10));

    World soft;
    soft.Add(Vector3(-3,0,0),Vector3(10,0,0),true); soft.balls[0].status.restitution=0;
    soft.Add(Vector3::Zero,Vector3::Zero);
    Complete(ContinuousBallStepper::Step(soft));
    assert(soft.hits==1 && Near(soft.balls[0].velocity.x,soft.balls[1].velocity.x));

    World overlap;
    overlap.Add(Vector3::Zero,Vector3::Zero,true); overlap.Add(Vector3::Zero,Vector3::Zero);
    Complete(ContinuousBallStepper::Step(overlap));
    assert(overlap.hits==0 && (overlap.balls[0].position-overlap.balls[1].position).Length()>=1.999f);

    World pierce;
    pierce.Add(Vector3(-10,0,0),Vector3(100,0,0),true);
    pierce.balls[0].status.abilities.pierce=true; pierce.balls[0].pierceLimit=3; pierce.balls[0].pierceRetention=1;
    pierce.Add(Vector3::Zero,Vector3::Zero); pierce.Add(Vector3(5,0,0),Vector3::Zero);
    Complete(ContinuousBallStepper::Step(pierce));
    assert(pierce.hits==2 && pierce.balls[0].pierceUses==2 && Near(pierce.balls[0].position.x,90));

    World reentry;
    reentry.Add(Vector3(-5,0,0),Vector3(20,0,0),true);
    reentry.balls[0].status.abilities.pierce=true; reentry.balls[0].pierceLimit=4; reentry.balls[0].pierceRetention=1;
    reentry.Add(Vector3::Zero,Vector3::Zero);
    reentry.walls.push_back({Vector3(6,0,-10),Vector3(6,0,10)});
    Complete(ContinuousBallStepper::Step(reentry));
    assert(reentry.hits==2 && reentry.balls[0].pierceUses==2);

    World anchor;
    anchor.Add(Vector3::Zero,Vector3::Zero,true); anchor.balls[0].status.abilities.anchor=true;
    anchor.balls[0].status.anchorKnockbackImmune=true;
    anchor.Add(Vector3(10,0,0),Vector3(-100,0,0));
    Complete(ContinuousBallStepper::Step(anchor));
    assert(anchor.hits==1 && anchor.balls[0].position==Vector3::Zero && anchor.balls[1].velocity.x>0);

    World pocket;
    pocket.Add(Vector3(-10,0,0),Vector3(1000,0,0),true);
    pocket.Add(Vector3(5,0,0),Vector3::Zero);
    pocket.pockets.push_back({Vector3::Zero,1});
    pocket.abortOnPocket=true;
    const auto retired=ContinuousBallStepper::Step(pocket);
    assert(!retired.limitReached && retired.advancedFraction<1 && pocket.hits==0 && !pocket.active[0]);

    World capped;
    capped.Add(Vector3::Zero,Vector3(10000,0,0),true);
    capped.walls={ {Vector3(-5,0,-10),Vector3(-5,0,10)}, {Vector3(5,0,-10),Vector3(5,0,10)} };
    const auto limited=ContinuousBallStepper::Step(capped);
    assert(limited.limitReached && limited.substeps==ContinuousBallStepper::MaxIterations && limited.advancedFraction<1);
    assert(std::abs(capped.balls[0].position.x)<=4.001f);

    World invalid;
    invalid.Add(Vector3::Zero,Vector3((std::numeric_limits<float>::quiet_NaN)(),0,0),true);
    const auto rejected=ContinuousBallStepper::Step(invalid);
    assert(rejected.limitReached && rejected.advancedFraction==0 && invalid.balls[0].position==Vector3::Zero);
    std::cout << "CCD/TOI: fast pairs, walls, caps, tangent, chain, overlap, low restitution, pierce/reentry, anchor, pocket, limits PASS\n";
}

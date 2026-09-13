#include "../SynchronizedBallStepper.h"
#include "../BallMechanics.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <vector>

// A one-dimensional elastic table adapter isolates the production time scheduler.
// Collision response uses the same BallMechanics function as the game.
struct Velocity1D { float value; float Length() const { return std::abs(value); } };
struct Body
{
    int id;
    float x, velocity;
    float radius = 1, mass = 1, transfer = 1;
    bool active = true;
    double elapsed = 0;
};
struct Table
{
    std::vector<Body> balls;
    std::vector<bool> moved;
    std::set<std::pair<int, int>> visited;
    std::vector<std::pair<int, int>> impacts;
    int pocketId = -1;
    bool stopOnImpact = false;
    float observedSpeed = 0;

    std::size_t Count() const { return balls.size(); }
    bool IsActive(std::size_t i) const { return balls[i].active; }
    float Radius(std::size_t i) const { return balls[i].radius; }
    Velocity1D Velocity(std::size_t i) const { return {balls[i].velocity}; }
    void BeginSubstep() { moved.assign(Count(), false); visited.clear(); }
    bool ShouldContinue() const { return !stopOnImpact || impacts.empty(); }
    void Move(std::size_t i, float interval)
    {
        float minimumRadius = balls[i].radius;
        for (const auto& ball : balls)
            if (ball.active) minimumRadius = (std::min)(minimumRadius, ball.radius);
        // Includes speed gained DURING this tick from heavy transfers.
        assert(std::abs(balls[i].velocity * interval) <= minimumRadius * 0.25f + 1.0e-5f);
        observedSpeed = (std::max)(observedSpeed, std::abs(balls[i].velocity));
        balls[i].x += balls[i].velocity * interval;
        balls[i].elapsed += interval;
        moved[i] = true;
    }
    void CheckBarrier() const
    {
        for (std::size_t i = 0; i < Count(); ++i)
            if (IsActive(i)) assert(moved[i]);
    }
    void ResolveEnvironment(std::size_t i)
    {
        CheckBarrier();
        if (balls[i].id == pocketId) balls[i].active = false;
    }
    void ResolvePair(std::size_t i, std::size_t j)
    {
        CheckBarrier();
        assert(visited.emplace((std::min)(balls[i].id, balls[j].id),
                               (std::max)(balls[i].id, balls[j].id)).second);
        auto& a = balls[i]; auto& b = balls[j];
        const float distance = std::abs(a.x - b.x);
        const float overlap = a.radius + b.radius - distance;
        if (overlap <= 0) return;
        const float normal = distance > 1.0e-5f ? (a.x < b.x ? -1.0f : 1.0f)
            : (a.velocity > b.velocity ? -1.0f : 1.0f);
        a.x += normal * overlap * 0.5f;
        b.x -= normal * overlap * 0.5f;
        const float av = a.velocity * normal, bv = b.velocity * normal;
        if (av - bv >= 0) return;
        auto impact = BallMechanics::ResolveNormalImpact(
            av, bv, a.mass, b.mass, 1, a.transfer, b.transfer, false, false);
        a.velocity += normal * (impact.first - av);
        b.velocity += normal * (impact.second - bv);
        impacts.emplace_back((std::min)(a.id,b.id), (std::max)(a.id,b.id));
    }
};

bool Near(double a, double b) { return std::abs(a - b) < 1.0e-5; }

int main()
{
    Table free{{{0,-10,1},{1,10,-1}}};
    auto full = SynchronizedBallStepper::Step(free);
    assert(Near(full.advancedFraction, 1) && !full.limitReached);
    assert(Near(free.balls[0].x,-9) && Near(free.balls[1].x,9));
    assert(Near(free.balls[0].elapsed,1) && Near(free.balls[1].elapsed,1));

    // The later-created moving ball must hit the earlier stationary ball.
    Table incoming{{{0,0,0},{1,3,-4}}};
    SynchronizedBallStepper::Step(incoming);
    assert(incoming.impacts.size() == 1);
    assert(Near(incoming.balls[0].velocity,-4) && Near(incoming.balls[1].velocity,0));
    Table reversed{{{1,3,-4},{0,0,0}}};
    SynchronizedBallStepper::Step(reversed);
    assert(reversed.impacts == incoming.impacts);
    assert(Near(incoming.balls[0].x,reversed.balls[1].x));
    assert(Near(incoming.balls[1].x,reversed.balls[0].x));

    Table headOn{{{0,-4,8},{1,4,-8}}};
    auto headResult = SynchronizedBallStepper::Step(headOn);
    assert(!headResult.limitReached && headOn.impacts.size()==1);
    assert(headOn.balls[0].velocity < 0 && headOn.balls[1].velocity > 0);
    assert(headOn.balls[0].x < headOn.balls[1].x);

    Table chain{{{0,0,8},{1,3,0},{2,6,0}}};
    SynchronizedBallStepper::Step(chain);
    assert((chain.impacts == std::vector<std::pair<int,int>>{{0,1},{1,2}}));
    assert(Near(chain.balls[2].velocity,8));
    assert(chain.balls[2].x > 6 && chain.balls[2].x < 14);
    for (const auto& ball : chain.balls) assert(Near(ball.elapsed,1));

    Table heavy{{{0,0,8,1,6,1.3f},{1,3,0}}};
    auto heavyResult = SynchronizedBallStepper::Step(heavy);
    assert(!heavyResult.limitReached && heavy.observedSpeed > 8);
    for (const auto& ball : heavy.balls) assert(Near(ball.elapsed,1));

    Table retired{{{0,0,8},{1,3,0},{2,0,100,1,1,1,false}}};
    SynchronizedBallStepper::Step(retired);
    assert(retired.impacts.size()==1 && retired.balls[2].x==0);
    Table pocket{{{0,0,8},{1,2,0}}};
    pocket.pocketId=0;
    SynchronizedBallStepper::Step(pocket);
    assert(!pocket.balls[0].active && pocket.impacts.empty() && pocket.balls[1].velocity==0);

    Table interrupted{{{0,0,8},{1,3,0}}};
    interrupted.stopOnImpact=true;
    auto interruptedResult = SynchronizedBallStepper::Step(interrupted);
    assert(interruptedResult.advancedFraction < 1 && !interruptedResult.limitReached);
    assert(Near(interrupted.balls[0].elapsed,interrupted.balls[1].elapsed));

    Table extreme{{{0,0,100000000}}};
    auto limited = SynchronizedBallStepper::Step(extreme);
    assert(limited.limitReached && limited.substeps == SynchronizedBallStepper::MaxSubsteps);
    assert(limited.advancedFraction < 1 && extreme.balls[0].x < 100);
    Table invalid{{{0,0,std::numeric_limits<float>::quiet_NaN()}}};
    assert(SynchronizedBallStepper::Step(invalid).limitReached && invalid.balls[0].x==0);

    std::cout << "Shared substeps: barriers, reverse pair, head-on, chain, acceleration, retirement, interruption and limits passed\n";
}

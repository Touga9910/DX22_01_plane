#include "../BossCombatRules.h"
#include "../ContinuousBallStepper.h"
#include <cassert>
#include <iostream>
#include <crtdbg.h>
#include "../json/json.hpp"
#include <fstream>
#include <iomanip>
#include <sstream>

using namespace BallPhysicsRules;

struct BossWorld
{
    Body ball;
    std::vector<Collision::Segment> walls;
    std::vector<Collision::Sphere> pockets;
    std::size_t Count() const { return 1; }
    bool IsActive(std::size_t) const { return true; }
    bool ShouldContinue() const { return true; }
    const Body& PhysicsBody(std::size_t) const { return ball; }
    const auto& Walls() const { return walls; }
    const auto& PocketSpheres() const { return pockets; }
    void BeginSubstep() {}
    void Move(std::size_t, float t) { ball.position += ball.velocity*t; }
    void ResolveEnvironment(std::size_t)
    {
        for (const auto& wall : BossWalls()) Wall(ball,wall,Vector3::Zero);
    }
    void ResolvePair(std::size_t, std::size_t) {}
};

int main(int argc, char** argv)
{
    if (argc == 3 && std::string(argv[1]) == "--fixture-checksum")
    {
        // Use the game's JSON number formatting for an explicitly supplied test save.
        nlohmann::json document;
        std::ifstream(argv[2]) >> document;
        std::uint64_t hash = 14695981039346656037ull;
        for (unsigned char c : document.at("payload").dump()) { hash ^= c; hash *= 1099511628211ull; }
        std::ostringstream text; text << std::hex << std::setw(16) << std::setfill('0') << hash;
        document["checksum"] = text.str();
        std::ofstream(argv[2]) << document.dump(2);
        return 0;
    }
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    using namespace BossCombatRules;
    State s;
    assert(DirectDamage(0,10,s)==1 && DirectDamage(9,1,s)==2 && DirectDamage(10,1,s)==3);
    s.BeginShot(); assert(!s.HitBreakBall() && s.armor==1);
    assert(s.HitBreakBall() && s.IsBroken() && s.shotsRemaining==2);
    assert(DirectDamage(10,1,s)==9);
    assert(!s.EndShot() && s.shotsRemaining==2); // triggering shot is excluded
    s.BeginShot(); assert(!s.HitBreakBall()); // does not refresh break
    assert(!s.EndShot() && s.shotsRemaining==1);
    s.BeginShot(); assert(s.EndShot() && !s.IsBroken() && s.armor==2);
    s.BeginShot(); assert(!s.EndShot() && s.armor==2);

    // Every build pushes neutral balls; pierce charges remain available for enemies.
    for (int build=0; build<5; ++build)
    {
        Body player, neutral;
        player.id=1; player.player=true; player.status.radius=2; player.status.restitution=0.8f;
        player.position=Vector3(-4,1,0); player.velocity=Vector3(4,0,0);
        neutral.id=2; neutral.breakBall=true; neutral.status.radius=2;
        neutral.position=Vector3(0,1,0);
        if (build==1) { player.status.mass=3; player.status.knockbackTransfer=1.5f; }
        if (build==2) { player.status.abilities.pierce=true; player.pierceLimit=3; }
        if (build==3) player.status.restitution=1;
        if (build==4) { player.status.abilities.anchor=true; player.status.anchorKnockbackImmune=true; }
        assert(Pair(player,neutral));
        assert(neutral.velocity.x>0 && player.pierceUses==0);
        if (build==1) assert(neutral.velocity.x>4);
        if (build==4)
        {
            Vector3 acc;
            assert(StopAnchor(player,acc) && player.velocity==Vector3::Zero && neutral.velocity.x>0);
        }
    }
    // Boss-only enclosure fills the central and corner pocket openings at high speed.
    for (const auto& velocity : {Vector3(0,0,1000),Vector3(1000,0,500),Vector3(-1000,0,-1000)})
    {
        BossWorld world;
        world.ball.boss=true; world.ball.enemy=true; world.ball.status.radius=6;
        world.ball.status.mass=18; world.ball.status.restitution=0.55f;
        world.ball.position=Vector3(0,1,0); world.ball.velocity=velocity;
        for (auto center : TableConfig::GetPocketCenters()) world.pockets.push_back({center,3});
        const auto r=ContinuousBallStepper::Step(world);
        assert(!r.limitReached && r.advancedFraction>0.999);
        assert(std::abs(world.ball.position.x)<=TableConfig::GetFieldWidth()/2-6+0.001);
        assert(std::abs(world.ball.position.z)<=TableConfig::GetFieldDepth()/2-6+0.001);
    }
    std::cout << "Boss Armor timing, rounding, neutral build impulses, pocket-safe CCD: passed\n";
}

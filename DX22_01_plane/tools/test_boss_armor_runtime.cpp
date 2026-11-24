#include "../BossCombatRules.h"
#include "../BossPocketRules.h"
#include "../ContinuousBallStepper.h"
#include "../TableGeometry.h"
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
    /// <summary>
    /// テスト盤面に置く球の個数を返す。
    /// </summary>
    std::size_t Count() const { return 1; }
    /// <summary>
    /// テスト用の球を常に処理対象として扱う。
    /// </summary>
    bool IsActive(std::size_t) const { return true; }
    /// <summary>
    /// テスト盤面で物理ステップの継続を許可する。
    /// </summary>
    bool ShouldContinue() const { return true; }
    /// <summary>
    /// テストの対象となる物理ボディを返す。
    /// </summary>
    const Body& PhysicsBody(std::size_t) const { return ball; }
    /// <summary>
    /// テスト盤面の壁一覧を返す。
    /// </summary>
    const auto& Walls() const { return walls; }
    /// <summary>
    /// テスト盤面のポケット一覧を返す。
    /// </summary>
    const auto& PocketSpheres() const { return pockets; }
    /// <summary>
    /// テスト用盤面ではサブステップの開始時に追加処理を行わない。
    /// </summary>
    void BeginSubstep() {}
    /// <summary>
    /// テスト用の球を速度と経過時間に応じて移動する。
    /// </summary>
    void Move(std::size_t, float t) { ball.position += ball.velocity*t; }
    /// <summary>
    /// テスト用の球とボス戦の壁の衝突を解決する。
    /// </summary>
    void ResolveEnvironment(std::size_t)
    {
        for (const auto& wall : BossWalls()) Wall(ball,wall,Vector3::Zero);
    }
    /// <summary>
    /// 単一の球を扱うテスト盤面向けに、球同士の衝突の呼び出し口を提供する。
    /// </summary>
    void ResolvePair(std::size_t, std::size_t) {}
};

/// <summary>
/// セーブのチェックサム計算を行うか、装甲ボスの攻撃や衝突処理を検証する。
/// </summary>
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

    // Opening pocket strips Armor, never the full-health boss's HP.
    State pocketState;
    auto pocket = ResolvePocket(pocketState, 60, 60);
    assert(pocket.applied && pocket.armorRemoved == 1 && pocket.damage == 0);
    assert(pocketState.armor == 1 && !pocketState.IsBroken());
    assert(!ResolvePocket(pocketState, 60, 60).applied); // same-shot bounce cannot farm
    pocketState.BeginShot();
    pocket = ResolvePocket(pocketState, 60, 60);
    assert(pocket.breakStarted && pocket.damage == 0 && pocketState.IsBroken());
    assert(!pocketState.EndShot() && pocketState.shotsRemaining == 2);
    pocketState.BeginShot();
    pocket = ResolvePocket(pocketState, 60, 60);
    assert(pocket.damage == 15 && !pocket.breakStarted);
    assert(!ResolvePocket(pocketState, 45, 60).applied);
    assert(!pocketState.EndShot() && pocketState.shotsRemaining == 1);
    pocketState.BeginShot();
    assert(ResolvePocket(pocketState, 12, 60).damage == 12); // real pocket finisher
    assert(pocketState.EndShot() && pocketState.armor == MaxArmor);
    State chain;
    chain.HitBreakBall(); chain.HitBreakBall();
    assert(ResolvePocket(chain, 52, 60).damage == 15); // break then pocket in one chain
    assert(PocketDamage(61) == 16 && PocketDamage(1) == 1);

    std::vector<Collision::Sphere> pockets;
    for (const auto& center : TableConfig::GetPocketCenters()) pockets.push_back({center, 3});
    const Vector3 entry(0, TableConfig::FIELD_HEIGHT, 36);
    const auto returned = BossPocketRules::FindReturnPosition(entry, 6, {}, pockets);
    assert(returned && returned->z < entry.z);
    std::vector<BossPocketRules::Obstacle> occupied{{*returned, 6}};
    const auto alternate = BossPocketRules::FindReturnPosition(entry, 6, occupied, pockets);
    assert(alternate && (*alternate-*returned).Length() >= 12.5f);
    occupied.push_back({Vector3::Zero, 1000});
    assert(!BossPocketRules::FindReturnPosition(entry, 6, occupied, pockets));

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

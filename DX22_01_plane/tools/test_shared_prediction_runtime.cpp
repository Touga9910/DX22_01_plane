#include "../BallPhysicsRules.h"
#include "../ShotRelicRules.h"
#include "../BallShotPrediction.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace BallPhysicsRules;
bool Near(float a, float b) { return std::abs(a-b) < 0.00001f; }

int main()
{
    {
        Body piercer, target;
        piercer.id = 1; piercer.player = true;
        target.id = 2; target.enemy = true;
        piercer.status.radius = target.status.radius = 2.4f;
        piercer.status.abilities.pierce = true;
        piercer.pierceLimit = 1;
        piercer.velocity = Vector3(0.2f,0,0);
        target.position = Vector3(4.8f,0,0);
        assert(Pair(piercer,target));
        assert(piercer.pierceUses == piercer.pierceLimit); // Last charge still needs to exit.
        int stopped = 0, extraHits = 0;
        Vector3 a, exit;
        bool finished = false, protectedOverlap = false;
        for (int tick=0;tick<200 && !finished;++tick)
        {
            const bool overlap = PierceExitDirection(piercer,target,exit);
            finished = PlayerFriction(piercer.velocity,a,piercer.status,0.02f,stopped,overlap?&exit:nullptr);
            if (overlap)
            {
                protectedOverlap = true;
                assert(!finished && stopped==0 && piercer.velocity.LengthSquared()>0.03f);
            }
            piercer.position += piercer.velocity;
            if (Pair(piercer,target)) ++extraHits;
        }
        assert(finished && protectedOverlap && extraHits==0 && piercer.pierceUses==1);
        assert((piercer.position-target.position).Length()>4.8f);
        piercer.position = target.position; piercer.velocity = Vector3::Zero;
        piercer.pierced = {target.id};
        assert(PierceExitDirection(piercer,target,exit) && exit.LengthSquared()>0.99f);
        piercer.velocity = Vector3(-0.1f,0,0); // Follow a wall-reflected direction.
        assert(PierceExitDirection(piercer,target,exit) && exit.x<0);
        piercer.pierced.clear();
        assert(!PierceExitDirection(piercer,target,exit)); // No new overlap bypass.
        piercer.pierced = {target.id}; target.enemy = false;
        assert(!PierceExitDirection(piercer,target,exit)); // Neutrals are not pierced.
    }
    // One reflection shows the incoming segment and one outgoing segment,
    // ending at the second contact. AI keeps the complete path.
    BallShotPrediction::Result preview;
    preview.path.resize(20);
    assert(preview.PreviewPointCount(1) == 20 && !preview.PreviewReachesLimit(1));
    preview.reflectionPathIndices = {4};
    assert(preview.PreviewPointCount(0) == 5 && preview.PreviewPointCount(1) == 20);
    preview.reflectionPathIndices = {4, 9, 14, 17};
    assert(preview.PreviewPointCount(1) == 10 && preview.PreviewReachesLimit(1));
    assert(preview.PreviewPointCount(2) == 15 && preview.PreviewPointCount(3) == 18);
    assert(preview.path.size() == 20);
    preview.reflectionPathIndices = {4, 4}; // Simultaneous corner contacts.
    assert(preview.PreviewPointCount(1) == 5);
    BallStatus status;
    status.radius = 2.0f;
    Vector3 velocity(0.1f, 0, 0), acceleration(1, 0, 0);
    int count = 0;
    for (int i = 0; i < 10; ++i) assert(!PlayerFriction(velocity, acceleration, status, 0.02f, count));
    assert(PlayerFriction(velocity, acceleration, status, 0.02f, count));
    assert(velocity == Vector3::Zero && acceleration == Vector3::Zero);

    // Selected power and friction affect travel; no implicit normalization to power 8.
    auto distance = [&](float power) {
        Vector3 v(power, 0, 0), a = Vector3::Zero;
        int stop = 0;
        float x = 0;
        while (!PlayerFriction(v, a, status, 0.02f, stop)) x += v.x;
        return x;
    };
    assert(distance(4) > distance(2) * 3);

    Body player, enemy;
    player.id = 1; enemy.id = 2;
    player.player = true; enemy.enemy = true;
    player.status = enemy.status = status;
    player.velocity = Vector3(4, 0, 0);
    enemy.position = Vector3(3.9f, 0, 0);
    player.status.abilities.pierce = true;
    player.pierceLimit = 2;
    assert(Pair(player, enemy));
    assert(player.pierceUses == 1 && Near(player.velocity.x, 3));
    assert(!Pair(player, enemy)); // One damage per continuous pierced overlap.
    player.position = Vector3(10, 0, 0);
    assert(!Pair(player, enemy) && player.pierced.empty());
    player.position = Vector3::Zero;
    assert(Pair(player, enemy) && player.pierceUses == 2);

    player = Body{}; enemy = Body{};
    player.id = 1; enemy.id = 2;
    player.player = true; enemy.enemy = true;
    player.status = enemy.status = status;
    player.status.abilities.anchor = true;
    player.status.anchorKnockbackImmune = true;
    enemy.position = Vector3(3.9f, 0, 0);
    enemy.velocity = Vector3(-3, 0, 0);
    assert(Pair(player, enemy));
    assert(player.position == Vector3::Zero && player.velocity == Vector3::Zero);
    assert(enemy.position.x >= 4 && enemy.velocity.x > 0);

    player.status.abilities.anchor = false;
    player.position = Vector3(9, 0, 0);
    player.velocity = Vector3(4, 0, 0);
    player.status.restitution = 0.5f;
    assert(Wall(player, {Vector3(10, 0, -10), Vector3(10, 0, 10)}, Vector3::Zero));
    assert(Near(player.position.x, 8) && Near(player.velocity.x, -2));
    assert(!Wall(player, {Vector3(10, 0, -10), Vector3(10, 0, 10)}, Vector3::Zero));
    assert(PocketHit(Vector3(-10, 50, 0), Vector3(10, 50, 0), 2, {Vector3::Zero, 1}));

    ShotRelicRules shot;
    shot.ballId = "player_bounce";
    shot.relics[static_cast<std::size_t>(RelicType::BankShot)] = true;
    shot.relics[static_cast<std::size_t>(RelicType::BounceBallSpring)] = true;
    shot.relics[static_cast<std::size_t>(RelicType::CollisionAttackUp)] = true;
    for (int i=0;i<5;++i) shot.Wall();
    auto damage = ContactDamage(player, enemy, 2, 1, false, false, shot);
    assert(damage.second == 7 && shot.bankConsumed && shot.bounceBonus == 0);
    shot.Contact(true);
    damage = ContactDamage(player, enemy, 3, 1, false, false, shot);
    assert(damage.second == 3 && shot.collisionBonus == 1);
    shot.ballId = "player_anchor";
    shot.relics[static_cast<std::size_t>(RelicType::AnchorBallChain)] = true;
    shot.Anchor();
    assert(shot.ConsumePlayerEnemyRelicDamageBonus() == 1);
    shot.ballId = "player_standard";
    shot.relics[static_cast<std::size_t>(RelicType::StandardBallScope)] = true;
    shot.wallContacts = 0; shot.launchPower = 4;
    assert(shot.ConsumePlayerEnemyRelicDamageBonus() == 1);
    shot.launchPower = 4.1f;
    assert(shot.ConsumePlayerEnemyRelicDamageBonus() == 0);
    assert(DirectionalDamage(10, Vector3::Zero, Vector3(0,0,-10), 0.5f) == 5);
    assert(DirectionalDamage(10, Vector3::Zero, Vector3(0,0,10), 0.5f) == 10);
    std::cout << "Shared friction, power, pierce contacts, anchor lock, wall, pocket and relic rules PASS\n";
}

#include "BallShotPrediction.h"
#include "BallCollisionComponent.h"
#include "BallComponent.h"
#include "BallPhysicsComponent.h"
#include "BallPhysicsWorld.h"
#include "EnemyBall.h"
#include "BreakBall.h"
#include "Game.h"
#include "GameObject.h"
#include "PlayerBall.h"
#include "PlayerBallData.h"
#include "Pocket.h"
#include "TableFrame.h"

#include <bit>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <numeric>

using namespace DirectX::SimpleMath;
using namespace BallShotPrediction;

namespace
{
    constexpr int MaxTicks = 1200;
    constexpr int MaxTotalSubsteps = 60000;
    constexpr std::size_t MaxPathPoints = 1000;

    std::vector<Ball> Capture(Game& game)
    {
        std::vector<Ball> result;
        for (auto* component : game.GetComponents<BallComponent>())
        {
            if (!component || !component->GetGameObject()) continue;
            auto* owner = component->GetGameObject();
            auto* collision = owner->GetComponent<BallCollisionComponent>();
            if (!collision) continue;
            // Binding normally happens in the real world's fixed update. Reading a
            // snapshot must not repair/change component state; initialization binds it.
            Ball ball;
            ball.physics.id = reinterpret_cast<std::uintptr_t>(component);
            ball.physics.position = component->GetPosition();
            ball.physics.velocity = component->GetVelocity();
            ball.physics.status = component->GetStatus();
            if (const auto* physical = owner->GetComponent<BallPhysicsComponent>())
            {
                ball.physics.status.radius = physical->GetRadius();
                ball.physics.status.mass = physical->GetMass();
                ball.physics.status.restitution = physical->GetRestitution();
                ball.physics.status.friction = physical->GetFriction();
                ball.acceleration = physical->GetAcceleration();
            }
            ball.hp = component->GetHP();
            ball.maxHp = component->GetMaxHP();
            ball.attack = component->GetAttack();
            ball.defense = component->GetDefense();
            ball.defeated = component->IsDefeated();
            if (const auto* player = owner->GetComponent<PlayerBall>())
            {
                ball.physics.player = true;
                ball.pocketed = player->IsPocketed();
            }
            if (const auto* enemy = owner->GetComponent<EnemyBall>())
            {
                ball.physics.enemy = true;
                ball.physics.boss = enemy->IsArmorBoss();
                ball.bossState = enemy->GetBossState();
                ball.enemyId = enemy->GetEnemyId();
                ball.pocketed = enemy->IsPocketed();
                ball.frontalMultiplier = enemy->GetFrontalDamageMultiplier();
                ball.pocketDamageRatio = enemy->GetPocketDamageRatio();
            }
            if (const auto* neutral = owner->GetComponent<BreakBall>())
            {
                ball.physics.breakBall = true;
                ball.pocketed = neutral->IsPocketed();
                ball.breakBallUsed = neutral->IsUsed();
            }
            ball.active = collision->CanSimulate();
            result.push_back(std::move(ball));
        }
        return result;
    }

    struct World
    {
        Result& result;
        std::vector<std::size_t> order;
        std::vector<Vector3> starts;
        std::vector<Collision::Segment> walls;
        std::vector<Collision::Sphere> pockets;
        Vector3 interior = Vector3::Zero, lastPathVelocity = Vector3::Zero;
        std::uintptr_t playerId = 0;
        float finisherRatio = 0.3f;
        int playerPocketDamage = 1;
        bool gameOver = false;
        const auto& PocketSpheres() const { return pockets; }
        const auto& Walls() const { return walls; }
        const auto& PhysicsBody(std::size_t i) const { return At(i).physics; }

        Ball& At(std::size_t i) { return result.balls[order[i]]; }
        const Ball& At(std::size_t i) const { return result.balls[order[i]]; }
        std::size_t Count() const { return order.size(); }
        bool IsActive(std::size_t i) const { return At(i).active; }
        float Radius(std::size_t i) const { return At(i).physics.status.radius; }
        Vector3 Velocity(std::size_t i) const { return At(i).physics.velocity; }
        bool ShouldContinue() const { return !gameOver; }

        int Attack(const Ball& ball) const
        {
            return ball.attack + (ball.physics.player ? result.shot.collisionBonus : 0);
        }

        void AddPoint(const Ball& ball, bool force = false)
        {
            if (ball.physics.id != playerId || !ball.active) return;
            const auto& p = ball.physics.position;
            const auto& v = ball.physics.velocity;
            const float lengths = lastPathVelocity.Length() * v.Length();
            const bool turned = lengths > 0.0001f && lastPathVelocity.Dot(v) / lengths < 0.9999f;
            if (!result.path.empty() && !force && !turned && (p - result.path.back()).LengthSquared() < 1.0f) return;
            if (!result.path.empty() && p == result.path.back()) return;
            if (result.path.size() >= MaxPathPoints) { result.pathTruncated = true; return; }
            result.path.push_back(p);
            lastPathVelocity = v;
        }

        void BeginSubstep()
        {
            for (std::size_t i = 0; i < Count(); ++i)
            {
                starts[i] = At(i).physics.position;
                AddPoint(At(i));
            }
        }
        void Move(std::size_t i, float interval) { At(i).physics.position += Velocity(i) * interval; }

        void RecordPreviewReflection(const Ball& ball, const Ball* other = nullptr)
        {
            if (ball.physics.id != playerId) return;
            // Only the first blocking contact supplies the short object-ball guide.
            // Later contacts (including enemies reached after a wall) stay hidden.
            if (result.reflectionPathIndices.empty() && other)
            {
                auto& guide = result.initialContactGuide;
                guide.hitBall = true;
                guide.playerPosition = ball.physics.position;
                guide.ballPosition = other->physics.position;
                guide.ballDirection = other->physics.velocity;
                if (guide.ballDirection.LengthSquared() > 0.0001f) guide.ballDirection.Normalize();
            }
            AddPoint(ball, true);
            if (!result.pathTruncated && !result.path.empty())
                result.reflectionPathIndices.push_back(result.path.size() - 1);
        }

        bool CheckPocket(Ball& ball, const Vector3& start)
        {
            if (ball.physics.boss) return false;
            for (const auto& pocket : pockets)
            {
                if (!BallPhysicsRules::PocketHit(start, ball.physics.position, ball.physics.status.radius, pocket)) continue;
                AddPoint(ball, true);
                const int hpBefore = ball.hp;
                if (ball.physics.player)
                {
                    ball.hp = (std::max)(0, ball.hp - playerPocketDamage);
                    ball.defeated = ball.hp == 0;
                    gameOver = ball.defeated;
                    if (!ball.defeated)
                    {
                        ball.pocketed = true;
                        ball.physics.position = Vector3(0, -1000, 0);
                    }
                }
                else if (ball.physics.enemy)
                {
                    if (!ball.defeated && ball.pocketDamageRatio > 0.0f)
                        ball.hp = (std::max)(0, ball.hp - BallMechanics::PocketDamage(ball.maxHp, ball.pocketDamageRatio));
                    if (ball.defeated || static_cast<float>(ball.hp) / (std::max)(1, ball.maxHp) <= finisherRatio + 0.0001f)
                    {
                        ball.hp = 0;
                        ball.defeated = true;
                    }
                    else
                    {
                        ball.pocketed = true;
                        ball.physics.position = Vector3(0, -1000, 0);
                    }
                    result.damage += hpBefore - ball.hp;
                }
                if (ball.physics.breakBall)
                {
                    ball.pocketed = true;
                    ball.breakBallUsed = false;
                    ball.physics.position = Vector3(0, -1000, 0);
                }
                ball.active = false;
                ball.physics.velocity = ball.acceleration = Vector3::Zero;
                return true;
            }
            return false;
        }

        void Environment(Ball& ball, const Vector3& start)
        {
            if (!ball.active || CheckPocket(ball, start)) return;
            if (ball.physics.boss)
                for (const auto& wall : BallPhysicsRules::BossWalls())
                    BallPhysicsRules::Wall(ball.physics, wall, Vector3::Zero);
            for (const auto& wall : walls)
            {
                if (BallPhysicsRules::Wall(ball.physics, wall, interior))
                {
                    if (ball.physics.player) result.shot.Wall();
                    RecordPreviewReflection(ball);
                }
            }
            CheckPocket(ball, start);
        }
        void ResolveEnvironment(std::size_t i) { Environment(At(i), starts[i]); }

        void Damage(Ball& target, int amount, const Vector3& source)
        {
            if (!target.physics.enemy || target.defeated) return;
            amount = BallPhysicsRules::DirectionalDamage(amount, target.physics.position, source, target.frontalMultiplier);
            const int before = target.hp;
            const int applied = target.physics.boss ? BossCombatRules::DirectDamage(amount, target.defense, target.bossState) :
                (std::max)(1, amount - target.defense);
            target.hp = (std::max)(0, target.hp - applied);
            target.defeated = target.hp == 0;
            result.damage += before - target.hp;
            // EnemyBall::TakeDamage intentionally preserves velocity after lethal contact.
        }

        void ResolvePair(std::size_t i, std::size_t j)
        {
            Ball& a = At(i);
            Ball& b = At(j);
            const Vector3 ap = a.physics.position, bp = b.physics.position;
            const Vector3 av = a.physics.velocity, bv = b.physics.velocity;
            const int aPierceUses = a.physics.pierceUses, bPierceUses = b.physics.pierceUses;
            if (BallPhysicsRules::Pair(a.physics, b.physics) && !gameOver)
            {
                // Count a physical ball response once, never the solver's repeated
                // overlap corrections or a straight-through piercing contact.
                if (a.physics.pierceUses == aPierceUses && b.physics.pierceUses == bPierceUses)
                {
                    RecordPreviewReflection(a, &b);
                    RecordPreviewReflection(b, &a);
                }
                if (a.physics.breakBall || b.physics.breakBall)
                {
                    auto hit = [&](Ball& neutral, Ball& boss) {
                        if (!neutral.physics.breakBall || !boss.physics.boss || boss.defeated) return;
                        boss.bossState.HitBreakBall();
                        const int before = boss.hp;
                        boss.hp = (std::max)(0, before - BossCombatRules::BreakBallDamage);
                        boss.defeated = boss.hp == 0;
                        result.damage += before - boss.hp;
                        result.bossFixedDamage += before - boss.hp;
                        ++result.breakBallHits;
                        neutral.active = false;
                        neutral.breakBallUsed = true;
                        neutral.pocketed = false;
                        neutral.physics.position = Vector3(0, -1000, 0);
                        neutral.physics.velocity = neutral.acceleration = Vector3::Zero;
                    };
                    hit(a, b); hit(b, a);
                    if (BallPhysicsRules::StopAnchor(a.physics, a.acceleration)) result.shot.Anchor();
                    if (BallPhysicsRules::StopAnchor(b.physics, b.acceleration)) result.shot.Anchor();
                }
                else
                {
                const bool playerEnemy = (a.physics.player && b.physics.enemy) || (b.physics.player && a.physics.enemy);
                const bool enemyEnemy = a.physics.enemy && b.physics.enemy;
                const auto damage = BallPhysicsRules::ContactDamage(a.physics, b.physics, Attack(a), Attack(b),
                    a.defeated, b.defeated, result.shot);
                Damage(a, damage.first, b.physics.position);
                Damage(b, damage.second, a.physics.position);
                if (playerEnemy)
                {
                    if (BallPhysicsRules::StopAnchor(a.physics, a.acceleration)) result.shot.Anchor();
                    if (BallPhysicsRules::StopAnchor(b.physics, b.acceleration)) result.shot.Anchor();
                }
                if (playerEnemy || enemyEnemy) result.shot.Contact(playerEnemy);
                }
                AddPoint(a, true);
                AddPoint(b, true);
            }
            if (a.active && (a.physics.position != ap || a.physics.velocity != av)) Environment(a, ap);
            if (b.active && (b.physics.position != bp || b.physics.velocity != bv)) Environment(b, bp);
        }

        void BeginTick()
        {
            order.clear();
            for (std::size_t i = 0; i < result.balls.size(); ++i)
                if (result.balls[i].active) order.push_back(i);
            for (auto i : order)
            {
                auto& body = result.balls[i].physics;
                body.velocity.y = 0.0f;
                const auto missing = [&](std::uintptr_t id) {
                    return std::none_of(order.begin(), order.end(), [&](std::size_t k) { return result.balls[k].physics.id == id; });
                };
                std::erase_if(body.pierced, missing);
                std::erase_if(body.touching, missing);
            }
            std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
                const auto& x = result.balls[a];
                const auto& y = result.balls[b];
                return BallPhysicsRules::OrderingKey(x.physics, x.hp, Attack(x), x.enemyId) <
                    BallPhysicsRules::OrderingKey(y.physics, y.hp, Attack(y), y.enemyId);
            });
            starts.resize(order.size());
        }
    };

    nlohmann::json BallJson(const Ball& ball)
    {
        auto vector = [](const Vector3& v) { return nlohmann::json::array({v.x, v.y, v.z}); };
        return {{"id", ball.physics.id}, {"player", ball.physics.player}, {"enemy_id", ball.enemyId},
            {"position", vector(ball.physics.position)}, {"velocity", vector(ball.physics.velocity)},
            {"hp", ball.hp}, {"active", ball.active}, {"defeated", ball.defeated}, {"pocketed", ball.pocketed},
            {"boss", ball.physics.boss}, {"break_ball", ball.physics.breakBall}, {"break_ball_used", ball.breakBallUsed},
            {"armor", ball.bossState.armor}, {"break_shots_remaining", ball.bossState.shotsRemaining},
            {"break_started_this_shot", ball.bossState.startedThisShot}};
    }

    bool VerificationEnabled()
    {
#ifdef _DEBUG
        char* flag = nullptr;
        std::size_t size = 0;
        _dupenv_s(&flag, &size, "DX22_TEST_PREDICTION");
        const bool enabled = flag != nullptr && std::string(flag) == "1";
        free(flag);
        return enabled;
#else
        return false;
#endif
    }
}

Result BallShotPrediction::Predict(Game& game, const PlayerBall& player, const Vector3& velocity, bool skipFirstPlayerFriction, const PlayerBallData* offer)
{
    const auto started = std::chrono::steady_clock::now();
    Result result;
    if (!std::isfinite(velocity.x) || !std::isfinite(velocity.z) || velocity.LengthSquared() <= 0.0001f) return result;
    result.balls = Capture(game);
    result.shot = game.MakePredictionShotRules(velocity.Length());
    if (offer) result.shot.ballId = offer->definitionId;
    World world{ result };
    world.playerId = reinterpret_cast<std::uintptr_t>(player.GetBall());
    world.finisherRatio = game.GetCurrentPocketFinisherRatio();
    world.playerPocketDamage = game.GetPlayerPocketDamageAmount();
    bool found = false;
    for (auto& ball : result.balls)
    {
        if (ball.physics.boss) ball.bossState.BeginShot();
        if (ball.physics.id != world.playerId) continue;
        if (!ball.active) return result;
        ball.physics.velocity = velocity;
        ball.physics.pierceLimit = game.GetPierceMaximumUses();
        ball.physics.pierceRetention = game.GetPierceSpeedRetention();
        if (offer)
        {
            ball.physics.status = offer->status;
            ball.defense = offer->status.defense + game.GetRelicDefenseBonus();
            ball.physics.pierceLimit = offer->status.pierceMaxUses;
            ball.physics.pierceRetention = offer->status.pierceSpeedRetention;
            if (offer->definitionId == "player_pierce" && game.HasRelic(RelicType::PierceBallCharger))
            { ++ball.physics.pierceLimit; ball.physics.pierceRetention = 1.0f; }
        }
        ball.attack = ball.physics.status.attack + game.GetRelicAttackBonus();
        world.AddPoint(ball, true);
        found = true;
    }
    if (!found) return result;
    for (auto* frame : game.GetComponents<TableFrame>())
    {
        if (!frame->IsEnabled() || !frame->GetGameObject()->IsActive()) continue;
        const auto walls = frame->GetWalls();
        world.walls.insert(world.walls.end(), walls.begin(), walls.end());
    }
    for (const auto& wall : world.walls) world.interior += wall.start + wall.end;
    if (!world.walls.empty()) world.interior /= static_cast<float>(world.walls.size() * 2);
    for (auto* pocket : game.GetComponents<Pocket>())
        if (pocket && pocket->GetGameObject() && pocket->GetGameObject()->IsActive()) world.pockets.push_back(pocket->GetSphere());
    int stoppedTicks = 0;
    for (int tick = 0; tick < MaxTicks; ++tick)
    {
        for (auto& ball : result.balls)
        {
            if (!ball.active) continue;
            if (ball.physics.player && !(tick == 0 && skipFirstPlayerFriction))
            {
                Vector3 exitDirection;
                bool exitingPierce = false;
                if (ball.physics.status.abilities.pierce && !ball.physics.pierced.empty())
                    for (const auto& other : result.balls)
                        if (other.active && BallPhysicsRules::PierceExitDirection(ball.physics, other.physics, exitDirection))
                        { exitingPierce = true; break; }
                if (ball.playerSimulation || exitingPierce)
                    ball.playerSimulation = !BallPhysicsRules::PlayerFriction(ball.physics.velocity, ball.acceleration,
                    ball.physics.status, ball.physics.status.friction, ball.stopCount,
                    exitingPierce ? &exitDirection : nullptr);
            }
            if (ball.physics.enemy || ball.physics.breakBall) BallPhysicsRules::EnemyFriction(ball.physics.velocity, ball.physics.status.friction);
        }
        world.BeginTick();
        const auto step = ContinuousBallStepper::Step(world);
        ++result.ticks;
        result.substeps += step.substeps;
        const bool stopped = std::all_of(result.balls.begin(), result.balls.end(), [](const Ball& b) {
            return b.physics.velocity.LengthSquared() < 0.03f;
        });
        stoppedTicks = stopped ? stoppedTicks + 1 : 0;
        if (step.limitReached) break;
        if (world.gameOver || stoppedTicks >= 11) { result.complete = true; break; }
        if (result.substeps >= MaxTotalSubsteps) break;
    }
    for (const auto& ball : result.balls) world.AddPoint(ball, true);
    result.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    return result;
}

std::uint64_t BallShotPrediction::WorldKey(Game& game)
{
    std::uint64_t hash = 14695981039346656037ull;
    auto add = [&](std::uint64_t value) { hash ^= value; hash *= 1099511628211ull; };
    auto scalar = [&](float value) { add(std::bit_cast<std::uint32_t>(value)); };
    auto vector = [&](const Vector3& v) { scalar(v.x); scalar(v.y); scalar(v.z); };
    for (const auto& ball : Capture(game))
    {
        add(ball.physics.id); add(ball.active); add(ball.hp); add(ball.maxHp); add(ball.attack); add(ball.defense);
        add(ball.defeated); add(ball.pocketed);
        add(ball.physics.boss); add(ball.physics.breakBall); add(ball.breakBallUsed);
        add(ball.bossState.armor); add(ball.bossState.shotsRemaining); add(ball.bossState.startedThisShot);
        vector(ball.physics.position); vector(ball.physics.velocity);
        const auto& s = ball.physics.status;
        scalar(s.radius); scalar(s.mass); scalar(s.restitution); scalar(s.friction); scalar(s.knockbackTransfer);
        add(s.abilities.pierce); add(s.abilities.anchor); add(s.pierceMaxUses); scalar(s.pierceSpeedRetention);
        scalar(s.anchorBrakeMultiplier); scalar(s.anchorStopSpeedSquared); add(s.anchorKnockbackImmune);
        scalar(ball.frontalMultiplier); scalar(ball.pocketDamageRatio);
    }
    const auto shot = game.MakePredictionShotRules(0.0f);
    for (bool owned : shot.relics) add(owned);
    for (unsigned char c : shot.ballId) add(c);
    scalar(game.GetCurrentPocketFinisherRatio()); add(game.GetPlayerPocketDamageAmount());
    for (auto* frame : game.GetComponents<TableFrame>())
    {
        add(frame->IsEnabled()); add(frame->GetGameObject()->IsActive());
        for (const auto& wall : frame->GetWalls()) { vector(wall.start); vector(wall.end); }
    }
    for (auto* pocket : game.GetComponents<Pocket>())
    {
        add(pocket->GetGameObject()->IsActive());
        const auto sphere = pocket->GetSphere(); vector(sphere.center); scalar(sphere.radius);
    }
    return hash;
}

void BallShotPrediction::WriteVerificationPrediction(Game& game, const PlayerBall& player, const Vector3& velocity, bool skip)
{
    if (!VerificationEnabled()) return;
    const auto before = WorldKey(game);
    const auto result = Predict(game, player, velocity, skip);
    nlohmann::json balls = nlohmann::json::array();
    for (const auto& ball : result.balls) balls.push_back(BallJson(ball));
    const nlohmann::json data = {{"physics_model", BallPhysicsWorld::ModelName}, {"complete", result.complete},
        {"path_truncated", result.pathTruncated}, {"path_points", result.path.size()},
        {"reflection_path_indices", result.reflectionPathIndices},
        {"preview_one_reflection_points", result.PreviewPointCount(1)},
        {"preview_one_reflection_limited", result.PreviewReachesLimit(1)},
        {"preview_direct_points", result.PreviewPointCount(0)},
        {"preview_direct_hit_ball", result.initialContactGuide.hitBall},
        {"ticks", result.ticks}, {"substeps", result.substeps}, {"milliseconds", result.milliseconds},
        {"world_unchanged", before == WorldKey(game)}, {"balls", balls},
        {"player_enemy_contacts", result.shot.playerEnemyContacts}, {"enemy_enemy_contacts", result.shot.enemyEnemyContacts}};
    std::filesystem::create_directories("runtime");
    std::ofstream("runtime/shot_prediction_expected.json") << data.dump(2);
}

void BallShotPrediction::WriteVerificationActual(Game& game)
{
    if (!VerificationEnabled()) return;
    nlohmann::json balls = nlohmann::json::array();
    for (const auto& ball : Capture(game)) balls.push_back(BallJson(ball));
    const nlohmann::json data = {{"balls", balls},
        {"player_enemy_contacts", game.GetCurrentShotPlayerEnemyCollisionCount()},
        {"enemy_enemy_contacts", game.GetCurrentShotEnemyEnemyCollisionCount()}};
    std::filesystem::create_directories("runtime");
    std::ofstream("runtime/shot_prediction_actual.json") << data.dump(2);
}

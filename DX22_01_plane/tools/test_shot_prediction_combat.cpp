// Exercise the production prediction contact adapter, including its private
// value-only world. The fail-fast adapters below are never used by these tests.
#include "../BallShotPrediction.cpp"

#include <cassert>
#include <iostream>

GameObject* Component::GetGameObject() const { std::abort(); }
bool BallCollisionComponent::CanSimulate() const { std::abort(); }
Vector3 BallComponent::GetPosition() const { std::abort(); }
int EnemyBall::GetAttack() const { std::abort(); }
Collision::Sphere Pocket::GetSphere() const { std::abort(); }
std::vector<Collision::Segment> TableFrame::GetWalls() const { std::abort(); }

namespace
{
    Ball MakePlayer()
    {
        Ball ball;
        ball.physics.id = 1;
        ball.physics.player = true;
        ball.physics.status.radius = 2.0f;
        ball.physics.velocity = Vector3(2.0f, 0.0f, 0.0f);
        ball.attack = 3;
        ball.hp = ball.maxHp = 20;
        return ball;
    }

    Ball MakeEnemy(std::uintptr_t id, const Vector3& position, int hp)
    {
        Ball ball;
        ball.physics.id = id;
        ball.physics.enemy = true;
        ball.physics.position = position;
        ball.physics.status.radius = 2.0f;
        ball.enemyId = "enemy_test";
        ball.attack = 7;
        ball.hp = ball.maxHp = hp;
        return ball;
    }

    void CheckChainAfterContact(bool reverse, int targetHp)
    {
        Result result;
        Ball player = MakePlayer();
        player.physics.status.chainImpactRadius = 10.0f;
        result.balls = { player,
            MakeEnemy(2, Vector3(4.0f, 0.0f, 0.0f), targetHp),
            MakeEnemy(3, Vector3(4.0f, 0.0f, 8.0f), 20) };
        result.shot.ballId = "player_chain_impact";
        result.shotStatus.heavyFinisherDamagePerCollision = 2.0f;
        result.shotStatus.heavyCollisionConsumeAmount = 3;
        result.heavyCollisions.stackCount = 3;
        World world{ result };
        world.playerId = 1;
        world.order = { 0, 1, 2 };
        world.starts.resize(3);
        world.ResolvePair(reverse ? 1 : 0, reverse ? 0 : 1);

        // Live BallCollisionComponent dispatches PlayerChainImpact using the
        // pre-contact defeat state, including when the direct hit is lethal.
        assert(result.balls[1].hp == (std::max)(0, targetHp - 3));
        assert(result.balls[2].hp == 11); // 3 contact attack + 3*2 Heavy bonus.
        assert(result.chainImpactHits == 1);
        assert(result.heavyFinisherConsumed);
        assert(result.heavyCollisions.stackCount == 0);
        world.ResolvePair(reverse ? 1 : 0, reverse ? 0 : 1);
        assert(result.balls[2].hp == 11); // Persistent contact cannot retrigger.
    }

    void CheckCorpseContact(bool killedThisShot)
    {
        Result result;
        result.balls = { MakePlayer(),
            MakeEnemy(2, Vector3(4.0f, 0.0f, 0.0f), 1),
            MakeEnemy(3, Vector3(8.0f, 0.0f, 0.0f), 20) };
        World world{ result };
        world.playerId = 1;
        world.order = { 0, 1, 2 };
        world.starts.resize(3);
        if (killedThisShot)
            world.ResolvePair(0, 1);
        else
        {
            result.balls[1].hp = 0;
            result.balls[1].defeated = true;
            result.balls[1].physics.enemy = false;
        }
        assert(result.balls[1].defeated);
        assert(!result.balls[1].physics.enemy);
        assert(world.Attack(result.balls[1]) == 0);
        result.balls[1].physics.velocity = Vector3(1.0f, 0.0f, 0.0f);
        world.ResolvePair(1, 2);
        assert(result.balls[2].hp == 20);
        assert(result.balls[2].physics.velocity.x > 0.0f); // Corpse still collides.
    }

    void CheckProtection()
    {
        Result result;
        result.balls = { MakeEnemy(1, Vector3::Zero, 20),
            MakeEnemy(2, Vector3(0.0f, 0.0f, 8.0f), 20) };
        auto& target = result.balls[0];
        auto& source = result.balls[1];
        target.defense = 1;
        source.boardAbility = EnemyBoardAbilityType::ProtectionResonator;
        source.boardAbilityRadius = 10.0f;
        source.protectionDamageReduction = 1;
        World world{ result };
        world.Damage(target, 3, Vector3::Zero, false);
        assert(target.hp == 19); // Same defense -> protection order as EnemyBall.
        world.Damage(target, 1, Vector3::Zero, false);
        assert(target.hp == 19); // Protection can absorb the minimum damage.
        source.physics.position.x = 100.0f;
        world.Damage(target, 3, Vector3::Zero, false);
        assert(target.hp == 17); // Moving outside the radius removes protection.
        source.physics.position.x = 0.0f;
        world.Damage(source, 100, Vector3::Zero, false);
        assert(source.defeated); // A resonator cannot protect itself.
        world.Damage(target, 3, Vector3::Zero, false);
        assert(target.hp == 15); // Its retained corpse provides no protection.
    }

    void CheckStoppedPierceMinion()
    {
        Result result;
        Ball minion;
        minion.physics.id = 1;
        minion.physics.minion = true;
        minion.physics.status.radius = 2.0f;
        minion.physics.status.friction = 0.02f;
        minion.physics.velocity = Vector3(0.18f, 0.0f, 0.0f);
        minion.attack = 7;
        minion.hp = minion.maxHp = 1;
        result.balls = { minion,
            MakeEnemy(2, Vector3(4.0f, 0.0f, 0.0f), 20) };
        result.balls[1].physics.velocity = Vector3(-1.0f, 0.0f, 0.0f);
        World world{ result };

        // PierceMinion::FixedUpdate destroys the small ball immediately after
        // friction puts it below the same IsStopped threshold used by runtime.
        BallPhysicsRules::EnemyFriction(
            result.balls[0].physics.velocity,
            result.balls[0].physics.status.friction);
        assert(result.balls[0].physics.velocity.LengthSquared() < 0.03f);
        world.BeginTick();
        assert(!result.balls[0].active);
        assert(result.balls[0].physics.velocity == Vector3::Zero);
        assert(world.Count() == 1);
        ContinuousBallStepper::Step(world);
        assert(result.balls[1].hp == 20); // No ghost hit or impulse from the minion.
        assert(result.balls[1].physics.velocity.x == -1.0f);

        // A moving minion, a stopped enemy corpse and a neutral remain physical.
        result.balls[0].active = true;
        result.balls[0].physics.velocity = Vector3(0.2f, 0.0f, 0.0f);
        result.balls[1].defeated = true;
        result.balls[1].physics.enemy = false;
        result.balls[1].physics.velocity = Vector3::Zero;
        Ball neutral;
        neutral.physics.id = 3;
        neutral.physics.breakBall = true;
        result.balls.push_back(neutral);
        world.BeginTick();
        assert(world.Count() == 3);
    }
}

int main()
{
    CheckChainAfterContact(false, 1);
    CheckChainAfterContact(true, 1);
    CheckChainAfterContact(false, 10);
    CheckChainAfterContact(true, 10);
    CheckCorpseContact(true);
    CheckCorpseContact(false);
    CheckProtection();
    CheckStoppedPierceMinion();
    std::cout << "shot prediction combat regressions passed\n";
}

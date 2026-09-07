#include "BallPhysicsWorld.h"

#include "BallCollisionComponent.h"
#include "BallComponent.h"
#include "BallPhysicsComponent.h"
#include "EnemyBall.h"
#include "Game.h"
#include "GameObject.h"
#include "PlayerBall.h"
#include "Pocket.h"
#include "TableFrame.h"

#include <algorithm>
#include <tuple>
#include <vector>

using namespace DirectX::SimpleMath;

namespace
{
    using SortKey = decltype(BallPhysicsRules::OrderingKey(BallPhysicsRules::Body{}, 0, 0, std::string{}));
    struct Body
    {
        BallComponent* ball;
        BallCollisionComponent* collision;
        Vector3 movementStart;
        SortKey sortKey;
    };

    struct WorldAdapter
    {
        Game& game;
        GameState initialState;
        std::vector<Body> bodies;
        std::vector<Collision::Segment> walls;
        std::vector<Pocket*> pockets;
        Vector3 interior = Vector3::Zero;
        std::vector<Collision::Sphere> pocketSpheres;
        const auto& PocketSpheres() const { return pocketSpheres; }
        const auto& Walls() const { return walls; }
        auto PhysicsBody(std::size_t i) const { return bodies[i].collision->CapturePhysicsBody(); }

        std::size_t Count() const { return bodies.size(); }
        bool IsActive(std::size_t i) const { return bodies[i].collision->CanSimulate(); }
        float Radius(std::size_t i) const { return bodies[i].ball->GetRadius(); }
        Vector3 Velocity(std::size_t i) const { return bodies[i].ball->GetVelocity(); }
        bool ShouldContinue() const { return game.GetGameState() == initialState; }

        void BeginSubstep()
        {
            for (Body& body : bodies) body.movementStart = body.ball->GetPosition();
        }
        void Move(std::size_t i, float interval)
        {
            bodies[i].ball->Translate(Velocity(i) * interval);
        }
        void ResolveEnvironment(std::size_t i)
        {
            bodies[i].collision->ResolveEnvironment(bodies[i].movementStart, walls, interior, pockets);
        }
        void ResolvePair(std::size_t i, std::size_t j)
        {
            const Vector3 firstStart = bodies[i].ball->GetPosition();
            const Vector3 secondStart = bodies[j].ball->GetPosition();
            const Vector3 firstVelocity = Velocity(i);
            const Vector3 secondVelocity = Velocity(j);
            bodies[i].collision->ResolveBallPair(*bodies[j].collision);
            // Corrected positions can enter a pocket or wall. Retire both sides
            // before either can participate in another pair in this substep.
            if (IsActive(i) && (bodies[i].ball->GetPosition() != firstStart || Velocity(i) != firstVelocity))
                bodies[i].collision->ResolveEnvironment(firstStart, walls, interior, pockets);
            if (IsActive(j) && (bodies[j].ball->GetPosition() != secondStart || Velocity(j) != secondVelocity))
                bodies[j].collision->ResolveEnvironment(secondStart, walls, interior, pockets);
        }
    };
}

ContinuousBallStepper::Result BallPhysicsWorld::Step(Game& game)
{
    WorldAdapter world{ game, game.GetGameState() };
    const std::vector<BallComponent*> balls = game.GetComponents<BallComponent>();
    const std::vector<Pocket*> pockets = game.GetComponents<Pocket>();
    const std::vector<TableFrame*> frames = game.GetComponents<TableFrame>();
    world.pockets = pockets;
    for (const auto* pocket : pockets)
        if (pocket && pocket->GetGameObject() && pocket->GetGameObject()->IsActive())
            world.pocketSpheres.push_back(pocket->GetSphere());
    std::vector<BallComponent*> activeBalls;
    activeBalls.reserve(balls.size());
    world.bodies.reserve(balls.size());
    for (BallComponent* ball : balls)
    {
        GameObject* owner = ball != nullptr ? ball->GetGameObject() : nullptr;
        if (owner == nullptr || !owner->IsActive() || owner->IsDestroyRequested()) continue;
        auto* collision = owner->GetComponent<BallCollisionComponent>();
        if (collision == nullptr) continue;
        collision->BindBall(*ball);
        if (!collision->CanSimulate()) continue;
        activeBalls.push_back(ball);
        world.bodies.push_back({ ball, collision, ball->GetPosition() });
    }
    for (Body& body : world.bodies)
    {
        body.collision->ForgetMissingContacts(activeBalls);
        body.ball->BeginPhysicsStep();
    }
    // Canonical spatial order, independent of storage/creation order for distinct
    // bodies. Equal-time contact clusters retain this stable sequential order.
    for (auto& body : world.bodies)
    {
        const auto* enemy = body.ball->GetGameObject()->GetComponent<EnemyBall>();
        body.sortKey = BallPhysicsRules::OrderingKey(body.collision->CapturePhysicsBody(), body.ball->GetHP(),
            body.ball->GetAttack(), enemy != nullptr ? enemy->GetEnemyId() : std::string());
    }
    std::stable_sort(world.bodies.begin(), world.bodies.end(),
        [](const Body& a, const Body& b) { return a.sortKey < b.sortKey; });

    for (TableFrame* frame : frames)
    {
        if (!frame->IsEnabled() || !frame->GetGameObject()->IsActive()) continue;
        const auto frameWalls = frame->GetWalls();
        world.walls.insert(world.walls.end(), frameWalls.begin(), frameWalls.end());
    }
    if (!world.walls.empty())
    {
        for (const auto& wall : world.walls) world.interior += wall.start + wall.end;
        world.interior /= static_cast<float>(world.walls.size() * 2);
    }
    const auto result = ContinuousBallStepper::Step(world);
    for (Body& body : world.bodies)
        if (body.collision->CanSimulate()) body.ball->FinishPhysicsStep();
    return result;
}

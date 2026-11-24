#include "BallPhysicsWorld.h"

#include "BallCollisionComponent.h"
#include "BallComponent.h"
#include "BallPhysicsComponent.h"
#include "EnemyBall.h"
#include "GameWorld.h"
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
        const BallPhysicsWorldContext& context;
        std::vector<Body> bodies;
        std::vector<Collision::Segment> walls;
        std::vector<Pocket*> pockets;
        Vector3 interior = Vector3::Zero;
        std::vector<Collision::Sphere> pocketSpheres;
        /// <summary>
        /// 物理計算に使うポケットの球形領域一覧を返す。
        /// </summary>
        const auto& PocketSpheres() const { return pocketSpheres; }
        /// <summary>
        /// 物理計算に使う壁の線分一覧を返す。
        /// </summary>
        const auto& Walls() const { return walls; }
        /// <summary>
        /// 指定番号のボールの衝突計算用状態を取得する。
        /// </summary>
        auto PhysicsBody(std::size_t i) const { return bodies[i].collision->CapturePhysicsBody(); }

        /// <summary>
        /// 今回の物理計算で扱うボールの個数を返す。
        /// </summary>
        std::size_t Count() const { return bodies.size(); }
        /// <summary>
        /// 指定番号のボールが衝突計算に参加できるかを返す。
        /// </summary>
        bool IsActive(std::size_t i) const { return bodies[i].collision->CanSimulate(); }
        /// <summary>
        /// 指定番号のボールの衝突半径を返す。
        /// </summary>
        float Radius(std::size_t i) const { return bodies[i].ball->GetRadius(); }
        /// <summary>
        /// 指定番号のボールの現在速度を返す。
        /// </summary>
        Vector3 Velocity(std::size_t i) const { return bodies[i].ball->GetVelocity(); }
        /// <summary>
        /// シミュレーションを続ける条件を実行側に問い合わせる。
        /// </summary>
        bool ShouldContinue() const { return context.shouldContinue(); }

        /// <summary>
        /// 物理サブステップの開始位置を全ボール分保存する。
        /// </summary>
        void BeginSubstep()
        {
            for (Body& body : bodies) body.movementStart = body.ball->GetPosition();
        }
        /// <summary>
        /// 指定時間だけボールを速度方向に進め、その移動を記録する。
        /// </summary>
        void Move(std::size_t i, float interval)
        {
			const Vector3 from = bodies[i].ball->GetPosition();
            bodies[i].ball->Translate(Velocity(i) * interval);
			context.traceMovement(
				*bodies[i].ball, from, bodies[i].ball->GetPosition());
        }
        /// <summary>
        /// サブステップ内に移動したボールと壁・ポケットの接触を処理する。
        /// </summary>
        void ResolveEnvironment(std::size_t i)
        {
            bodies[i].collision->ResolveEnvironment(bodies[i].movementStart, walls, interior, pockets);
        }
        /// <summary>
        /// 2球の衝突を解決し、速度や位置の変化を移動記録へ反映する。
        /// </summary>
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

/// <summary>
/// 活動中のボール、壁、ポケットを集め、連続衝突判定で物理ステップを進める。
/// </summary>
ContinuousBallStepper::Result BallPhysicsWorld::Step(
    const BallPhysicsWorldContext& context)
{
    WorldAdapter world{ context };
    const std::vector<BallComponent*> balls =
        context.world.GetComponents<BallComponent>();
    const std::vector<Pocket*> pockets =
        context.world.GetComponents<Pocket>();
    const std::vector<TableFrame*> frames =
        context.world.GetComponents<TableFrame>();
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
		const int combatAttack = enemy != nullptr &&
			!enemy->CanParticipateInCombat()
			? 0
			: body.ball->GetAttack();
        body.sortKey = BallPhysicsRules::OrderingKey(body.collision->CapturePhysicsBody(), body.ball->GetHP(),
			combatAttack, enemy != nullptr ? enemy->GetEnemyId() : std::string());
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

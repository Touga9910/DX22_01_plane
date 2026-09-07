#include "BreakBall.h"
#include "BallComponent.h"
#include "BallPhysicsRules.h"
#include "BallRenderComponent.h"
#include "BalanceLogger.h"
#include "Camera.h"
#include "EnemyBall.h"
#include "Game.h"
#include "GameObject.h"
#include "Pocket.h"
#include "TableConfig.h"
#include "TableFrame.h"

using namespace DirectX::SimpleMath;

void BreakBall::Awake()
{
    m_Ball = GetGameObject()->GetComponent<BallComponent>();
    auto* render = GetGameObject()->GetComponent<BallRenderComponent>();
    if (!m_Ball || !render) { GetGameObject()->Destroy(); return; }
    BallStatus status;
    status.attack = 0;
    status.radius = 2.5f;
    status.mass = 1.0f;
    status.restitution = 0.8f;
    status.friction = 0.025f;
    m_Ball->SetMaxHP(1);
    m_Ball->SetStatus(status);
    render->LoadModel("assets/model/GolfBall/golf_ball.obj", "assets/model/GolfBall");
    render->SetTint(Color(1.0f, 0.8f, 0.08f, 1.0f));
    const float scale = status.radius / (std::max)(0.001f, render->GetModelBaseRadius());
    m_Ball->SetScale(Vector3(scale));
    m_Ball->SetPocketHandler([this]() { Deactivate(true); });
    GetGameObject()->SetActive(false);
}

void BreakBall::FixedUpdate()
{
    BallPhysicsRules::EnemyFriction(m_Ball->GetMutableVelocity(), m_Ball->GetStatus().friction);
}

void BreakBall::Draw()
{
    auto* camera = Game::GetCamera();
    if (!camera || !m_Ball) return;
    camera->SetCamera();
    auto* render = GetGameObject()->GetComponent<BallRenderComponent>();
    render->BeginDraw();
    const Matrix world = Matrix::CreateScale(m_Ball->GetScale()) *
        Matrix::CreateFromQuaternion(m_Ball->GetMutableRollingRotation()) *
        Matrix::CreateTranslation(m_Ball->GetPosition());
    render->DrawMesh(world);
}

void BreakBall::OnDestroy()
{
    if (m_Ball) m_Ball->SetPocketHandler({});
}

void BreakBall::Deactivate(bool pocketed)
{
    m_Pocketed = pocketed;
    m_Used = !pocketed;
    m_Ball->ResetAtPosition(Vector3(0, -1000, 0));
    GetGameObject()->SetActive(false);
    if (pocketed) BalanceLogger::GetInstance().RecordEvent("break_ball_pocketed", {{"ball_id", m_Index}});
}

void BreakBall::HitBoss(EnemyBall& boss)
{
    if (!GetGameObject()->IsActive() || boss.IsDefeated() || !boss.IsArmorBoss()) return;
    boss.HitBreakBall(m_Index);
    Deactivate(false);
}

bool BreakBall::Reposition()
{
    if (!m_Ball || GetGameObject()->IsActive()) return true;
    auto& game = *Game::GetInstance();
    const auto balls = game.GetComponents<BallComponent>();
    const auto pockets = game.GetComponents<Pocket>();
    const auto frames = game.GetComponents<TableFrame>();
    const float radius = m_Ball->GetRadius();
    auto safe = [&](const Vector3& p) {
        if (std::abs(p.x) + radius + 1 > TableConfig::GetFieldWidth() * 0.5f ||
            std::abs(p.z) + radius + 1 > TableConfig::GetFieldDepth() * 0.5f) return false;
        for (auto* ball : balls)
        {
            if (ball == m_Ball || !ball->GetGameObject()->IsActive() || ball->GetGameObject()->IsDestroyRequested()) continue;
            const float distance = radius + ball->GetRadius() + 1.0f;
            if ((p - ball->GetPosition()).LengthSquared() < distance * distance) return false;
        }
        for (auto* pocket : pockets)
        {
            if (!pocket->GetGameObject()->IsActive()) continue;
            const auto sphere = pocket->GetSphere();
            if (BallPhysicsRules::PocketHit(p, p, radius + 1.0f, sphere)) return false;
        }
        for (auto* frame : frames)
            for (const auto& wall : frame->GetWalls())
                if ((p - BallCcdGeometry::ClosestXZ(p, wall)).Length() < radius + 1.0f) return false;
        return true;
    };
    // Stable preferred positions, followed by a complete deterministic interior grid.
    std::vector<Vector3> candidates = {
        Vector3(m_Index == 0 ? -4.0f : 4.0f, TableConfig::FIELD_HEIGHT, 10.0f),
        Vector3(m_Index == 0 ? -24.0f : 24.0f, TableConfig::FIELD_HEIGHT, -8.0f)
    };
    for (float z = -24; z <= 24; z += 8)
        for (float x = -56; x <= 56; x += 8) candidates.emplace_back(x, TableConfig::FIELD_HEIGHT, z);
    for (const auto& p : candidates)
    {
        if (!safe(p)) continue;
        m_Ball->ResetAtPosition(p);
        m_Ball->ResetShotAbilityState();
        m_Used = m_Pocketed = false;
        GetGameObject()->SetActive(true);
        BalanceLogger::GetInstance().RecordEvent("break_ball_repositioned",
            {{"ball_id", m_Index}, {"position", {p.x, p.y, p.z}}});
        return true;
    }
    // Never force an overlapping spawn; retry at the next shot boundary.
    BalanceLogger::GetInstance().RecordEvent("break_ball_reposition_deferred", {{"ball_id", m_Index}});
    return false;
}

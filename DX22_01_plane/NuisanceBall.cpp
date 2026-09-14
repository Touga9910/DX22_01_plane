#include "NuisanceBall.h"

#include "BallComponent.h"
#include "BallPhysicsRules.h"
#include "BallRenderComponent.h"
#include "Camera.h"
#include "Game.h"
#include "GameObject.h"
#include "PlayerBall.h"
#include "TableConfig.h"

using namespace DirectX::SimpleMath;

void NuisanceBall::Awake()
{
    m_Ball = GetGameObject()->GetComponent<BallComponent>();
    auto* render = GetGameObject()->GetComponent<BallRenderComponent>();
    if (m_Ball == nullptr || render == nullptr)
    {
        GetGameObject()->Destroy();
        return;
    }

    BallStatus status;
    status.attack = 0;
    status.defense = 0;
    status.radius = m_Data.radius;
    status.mass = m_Data.mass;
    status.restitution = m_Data.restitution;
    status.friction = m_Data.friction;
    m_Ball->SetMaxHP(1);
    m_Ball->SetStatus(status);
    render->LoadModel("assets/model/GolfBall/golf_ball.obj", "assets/model/GolfBall");

    const bool defenseDown = m_Data.debuffs.Has(StatusEffectType::DefenseDown);
    render->SetTint(defenseDown
        ? Color(0.25f, 0.45f, 1.0f, 1.0f)
        : Color(0.72f, 0.22f, 0.95f, 1.0f));
    const float scale = status.radius /
        (std::max)(0.001f, render->GetModelBaseRadius());
    m_Ball->SetScale(Vector3(scale));
    m_Position.y = TableConfig::FIELD_HEIGHT;
    m_Ball->ResetAtPosition(m_Position);
    m_Ball->SetPocketHandler([this]() { Pocket(); });
}

void NuisanceBall::FixedUpdate()
{
    if (m_Ball != nullptr)
    {
        BallPhysicsRules::EnemyFriction(
            m_Ball->GetMutableVelocity(),
            m_Ball->GetStatus().friction);
    }
}

void NuisanceBall::Draw()
{
    Camera* camera = Game::GetCamera();
    if (camera == nullptr || m_Ball == nullptr) return;
    auto* render = GetGameObject()->GetComponent<BallRenderComponent>();
    if (render == nullptr) return;

    camera->SetCamera();
    render->BeginDraw();
    const Matrix world = Matrix::CreateScale(m_Ball->GetScale()) *
        Matrix::CreateFromQuaternion(m_Ball->GetMutableRollingRotation()) *
        Matrix::CreateTranslation(m_Ball->GetPosition());
    render->DrawMesh(world);
}

void NuisanceBall::OnDestroy()
{
    if (m_Ball != nullptr) m_Ball->SetPocketHandler({});
}

void NuisanceBall::Pocket()
{
    if (m_Ball == nullptr) return;
    m_Ball->GetMutableVelocity() = Vector3::Zero;
    m_Ball->GetMutableAcceleration() = Vector3::Zero;
    GetGameObject()->Destroy();
    for (PlayerBall* player : Game::GetInstance()->GetComponents<PlayerBall>())
        if (player != nullptr) player->RefreshNuisanceDebuffs();
}

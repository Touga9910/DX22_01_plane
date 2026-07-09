#include "EnemyBall.h"
#include "PlayerBall.h"
#include "Game.h"
#include "Ground.h"
#include "Camera.h"
#include "imgui/imgui.h"

using namespace DirectX::SimpleMath;

//=======================================
// 初期化処理
//=======================================
void EnemyBall::Init()
{
    EnemyData data;
    Init(data);
}

// EnemyDataからの情報を元に初期化する関数
void EnemyBall::Init(const EnemyData& data)
{
    m_EnemyData = data;

    SetStatus(m_EnemyData.status);

    LoadModel(
        m_EnemyData.modelFilePath.c_str(),
        m_EnemyData.textureDirectory.c_str()
    );

    m_Transform.position = m_EnemyData.initPosition;

    m_Transform.scale = m_EnemyData.scale;
    UpdateRadius();

    std::vector<Ground*> grounds = Game::GetInstance()->GetObjects<Ground>();
    if (!grounds.empty())
    {
        m_Transform.position.y = grounds[0]->GetFieldHeight();
    }

    m_Velocity = Vector3::Zero;
    m_Acceleration = Vector3::Zero;
    m_CurrentFrame = 0;

    ResetDefeated();
}

//=======================================
// 更新処理
//=======================================
void EnemyBall::Update()
{
    if (IsDefeated())
    {
        return;
    }

    m_CurrentFrame++;

    // --- 摩擦・減速の計算 (PlayerBallの挙動と合わせる場合) ---
    if (m_Velocity.LengthSquared() > 0.001f)
    {
        if (m_Velocity.LengthSquared() < 0.03f)
        {
            m_Velocity = Vector3::Zero;
        }
        else
        {
            float deceleratisonPower = m_Friction;
            Vector3 deceleration = -m_Velocity;
            deceleration.Normalize();
            m_Velocity += deceleration * deceleratisonPower;
        }
    }

    // --- 物理演算の更新 (BallBaseの壁判定や移動、転がり回転を呼び出す) ---
    UpdatePhysics();
}

//=======================================
// 描画処理
//=======================================
void EnemyBall::Draw(Camera* cam)
{
    if (IsDefeated())
    {
        return;
    }
    cam->SetCamera();

    m_Shader.SetGPU();
    m_MeshRenderer.BeforeDraw();

    // 行列の作成（BallBaseが計算してくれた m_RollingRotation を適用）
    Matrix rDirection = Matrix::CreateFromYawPitchRoll(m_Transform.rotation.y, m_Transform.rotation.x, m_Transform.rotation.z);
    Matrix rRolling = Matrix::CreateFromQuaternion(m_RollingRotation);
    Matrix r = rDirection * rRolling;

    Matrix t = Matrix::CreateTranslation(m_Transform.position);
    Matrix s = Matrix::CreateScale(m_Transform.scale);

    Matrix worldmtx = s * r * t;

    // BallBaseの共通描画関数を呼び出す
    DrawMesh(worldmtx);
}

//=======================================
// 終了処理
//=======================================
void EnemyBall::Uninit()
{
    // 必要に応じた解放処理
}

// ======================================
// 倒された時の処理
// ======================================
void EnemyBall::Defeat()
{
    BallBase::Defeat();

    // 敵専用の倒された時の処理を書くならここ
    // 例：撃破エフェクト、スコア加算、ドロップ抽選など
}

void EnemyBall::Attack(PlayerBall* player)
{
    if (IsDefeated())
    {
        return;
    }

    if (player == nullptr)
    {
        return;
    }

    if (player->IsDefeated())
    {
        return;
    }

    player->TakeDamage(GetAttack());
}

void EnemyBall::ApplyHotReloadData(const EnemyData& data)
{
    // 別の敵IDのデータを誤って適用しない
    if (m_EnemyData.id != data.id)
    {
        return;
    }

    // 敵データを更新
    m_EnemyData.status = data.status;
    m_EnemyData.rewardMoney = data.rewardMoney;
    m_EnemyData.rewardExp = data.rewardExp;
    m_EnemyData.scale = data.scale;

    // HP割合を維持したままステータス更新
    ApplyStatusKeepHpRate(data.status);

    // スケールも反映
    m_Transform.scale = data.scale;
    UpdateRadius();

    std::cout << "[HotReload] Enemy updated: "
        << m_EnemyData.id
        << " HP: " << m_EnemyData.status.maxHp
        << " Attack: " << m_EnemyData.status.attack
        << " Defense: " << m_EnemyData.status.defense
        << std::endl;
}

// ImGUIによるステータス確認
void EnemyBall::DrawImGui(const std::string& label)
{
    
    BallBase::DrawImGui(label); // 共通UIを呼ぶ

    /*
    if (ImGui::CollapsingHeader((label + " Detail").c_str()))
    {
        ImGui::Text("Frame: %d", m_CurrentFrame);
    }
    */
    
}
#include "EnemyBall.h"
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
    // 1. 敵用のモデルを読み込む (Playerと違うモデルやテクスチャを指定可能)
    LoadModel("assets/model/GolfBall/golf_ball.obj", "assets/model/GolfBall");

    // 2. 初期座標の設定 (あとで配置マネージャーなどから設定できるようにすると良い)
    m_Transform.position = m_InitPosition;

    // スケール調整
    m_Transform.scale = Vector3(2.0f, 2.0f, 2.0f);
    UpdateRadius();

    // Groundから台の高さを取得して合わせる
    std::vector<Ground*> grounds = Game::GetInstance()->GetObjects<Ground>();
    if (!grounds.empty())
    {
        m_Transform.position.y = grounds[0]->GetFieldHeight();
    }

    // 初期速度（最初は止まっている、あるいはゆっくり動かす）
    m_Velocity = Vector3::Zero;
}

//=======================================
// 更新処理
//=======================================
void EnemyBall::Update()
{
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

    // --- [発展要素] 簡単な敵のAI（例：一定時間ごとにランダムな方向に動く） ---
    /*
    if (m_CurrentFrame % 180 == 0) // 3秒に1回
    {
        float angle = (rand() % 360) * 3.141592f / 180.0f;
        float speed = 2.0f;
        m_Velocity = Vector3(cos(angle) * speed, 0.0f, sin(angle) * speed);
    }
    */

    // --- 物理演算の更新 (BallBaseの壁判定や移動、転がり回転を呼び出す) ---
    UpdatePhysics();
}

//=======================================
// 描画処理
//=======================================
void EnemyBall::Draw(Camera* cam)
{
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

// ImGUIによるステータス確認
void EnemyBall::DrawImGui(const std::string& label)
{
    BallBase::DrawImGui(label); // 共通UIを呼ぶ

    if (ImGui::CollapsingHeader((label + " Detail").c_str()))
    {
        ImGui::Text("Frame: %d", m_CurrentFrame);
    }
}
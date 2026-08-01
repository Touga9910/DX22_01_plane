#include "EnemyBall.h"

#include "Camera.h"
#include "Game.h"
#include "Ground.h"
#include "PlayerBall.h"
#include "imgui/imgui.h"
#include "GameObject.h"

#include <iostream>

using namespace DirectX::SimpleMath;

void EnemyBall::Awake()
{
    m_Ball = GetGameObject()->AddComponent<BallComponent>();
    if (m_InitialData.has_value())
    {
        Init(*m_InitialData);
        m_InitialData.reset();
    }
    else
    {
        Init();
    }
}

void EnemyBall::Draw()
{
    Draw(Game::GetCamera());
}

void EnemyBall::OnDestroy()
{
    Uninit();
}

void EnemyBall::Init()
{
    // デフォルトのEnemyDataを使って初期化する
    EnemyData data;
    Init(data);
}

void EnemyBall::Init(const EnemyData& data)
{
    m_EnemyData = data;                        // 読み込んだ敵データを保持

    SetStatus(m_EnemyData.status);             // ステータスを反映

    m_Ball->LoadModel(
        m_EnemyData.modelFilePath.c_str(),
        m_EnemyData.textureDirectory.c_str()
    );

    m_Ball->GetMutableTransform().position = m_EnemyData.initPosition; // 初期位置を反映
    m_Ball->GetMutableTransform().scale = m_EnemyData.scale;        // スケールを反映
    m_Ball->UpdateRadius();                                  // スケールに合わせて半径を更新

    std::vector<Ground*> grounds = Game::GetInstance()->GetObjects<Ground>();
    if (!grounds.empty())
    {
        // Groundがある場合は、台の高さに合わせてY座標を補正する
        m_Ball->GetMutableTransform().position.y = grounds[0]->GetFieldHeight();
    }

    m_Ball->GetMutableVelocity() = Vector3::Zero;             // 速度を初期化
    m_Ball->GetMutableAcceleration() = Vector3::Zero;             // 加速度を初期化
    m_CurrentFrame = 0;                         // フレームカウントを初期化

    m_Ball->ResetDefeated();                            // 撃破状態を解除
}

void EnemyBall::Update()
{
    m_CurrentFrame++;

    // --- 摩擦・減速の計算 (PlayerBallの挙動と合わせる場合) ---
    if (m_Ball->GetMutableVelocity().LengthSquared() > 0.001f)
    {
        if (m_Ball->GetMutableVelocity().LengthSquared() < 0.03f)
        {
            m_Ball->GetMutableVelocity() = Vector3::Zero;
        }
        else
        {
            float decelerationPower = m_Ball->GetMutableFriction();   // 摩擦による減速量
            Vector3 deceleration = -m_Ball->GetMutableVelocity();     // 速度と逆方向に減速させる
            deceleration.Normalize();
            m_Ball->GetMutableVelocity() += deceleration * decelerationPower;
        }
    }

    // --- 物理演算の更新 (BallComponentの壁判定や移動、転がり回転を呼び出す) ---
    m_Ball->UpdatePhysics();
}

void EnemyBall::Draw(Camera* cam)
{
    cam->SetCamera();

    m_Ball->BeginDraw();

    // 行列の作成（BallComponentが計算した m_Ball->GetMutableRollingRotation() を適用）
    Matrix rDirection = Matrix::CreateFromYawPitchRoll(
        m_Ball->GetMutableTransform().rotation.y,
        m_Ball->GetMutableTransform().rotation.x,
        m_Ball->GetMutableTransform().rotation.z
    );
    Matrix rRolling = Matrix::CreateFromQuaternion(m_Ball->GetMutableRollingRotation());
    Matrix r = rDirection * rRolling;

    Matrix t = Matrix::CreateTranslation(m_Ball->GetMutableTransform().position);
    Matrix s = Matrix::CreateScale(m_Ball->GetMutableTransform().scale);

    Matrix worldmtx = s * r * t;

    // BallComponentの共通描画関数を呼び出す
    m_Ball->DrawMesh(worldmtx);
}

void EnemyBall::Uninit()
{
    // 必要に応じた解放処理
}

void EnemyBall::Defeat()
{
    m_Ball->Defeat();

    // 敵専用の倒された時の処理を書くならここ
    // 例：撃破エフェクト、スコア加算、ドロップ抽選など
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
    m_Ball->GetMutableTransform().scale = data.scale;
    m_Ball->UpdateRadius();

    std::cout << "[HotReload] Enemy updated: "
        << m_EnemyData.id
        << " HP: " << m_EnemyData.status.maxHp
        << " Attack: " << m_EnemyData.status.attack
        << " Defense: " << m_EnemyData.status.defense
        << std::endl;
}

void EnemyBall::ApplyStatusKeepHpRate(const BallStatus& status)
{
    if (m_Ball->IsDefeated())
    {
        // 撃破済みの場合はHP割合を計算せず、ステータス値だけ更新する
        ApplyStatusValuesOnly(status);
        return;
    }

    float hpRate = 1.0f;                       // 現在HPの割合

    if (GetMaxHP() > 0)
    {
        hpRate = static_cast<float>(GetHP()) / static_cast<float>(GetMaxHP());
    }

    ApplyStatusValuesOnly(status);             // 最大HPなどのステータスを更新

    int newHp = static_cast<int>(GetMaxHP() * hpRate); // 更新後の最大HPに合わせた現在HP

    if (newHp < 1)
    {
        newHp = 1;
    }

    if (newHp > GetMaxHP())
    {
        newHp = GetMaxHP();
    }

    SetHP(newHp);
}

void EnemyBall::ApplyStatusValuesOnly(const BallStatus& status)
{
    // HPや撃破状態を初期化せず、ステータス構造体だけを差し替える
    m_Ball->ApplyStatusValuesOnly(status);
}

void EnemyBall::DrawImGui(const std::string& label)
{
    m_Ball->DrawImGui(label); // 共通UIを呼ぶ
}

#include "PlayerBall.h"
#include "BallPhysicsRules.h"
#include "BallCollisionComponent.h"
#include "GameBattleCommands.h"
#include "GameBattleEvents.h"
#include "GameBattleView.h"
#include "GamePlayerState.h"
#include "GameRuntime.h"
#include "GameShotPrediction.h"
#include "GameShotTraceView.h"
#include "GameWorld.h"
#include "input.h"
#include "Ground.h"
#include "TableFrame.h"
#include "Camera.h"
#include "Application.h"
#include "imgui/imgui.h"
#include "GameObject.h"
#include "NuisanceBall.h"
#include "PlayerBallData.h"

#include <algorithm>
#include <chrono>
#include <cmath>

using namespace std;
using namespace DirectX::SimpleMath;

void PlayerBall::Awake()
{
	GameObject* owner = GetGameObject();
	if (owner == nullptr)
	{
		return;
	}

	m_Ball = owner->GetComponent<BallComponent>();
	m_RenderComponent = owner->GetComponent<BallRenderComponent>();
	if (m_Ball == nullptr || m_RenderComponent == nullptr)
	{
		owner->Destroy();
		return;
	}

	m_Ball->SetPocketHandler([this]() { OnPocketHit(); });
	m_Ball->SetDefeatHandler([this]() { Defeat(); });
	Init();
}

void PlayerBall::Draw()
{
	Draw(&GameRuntime::MainCamera());
}

void PlayerBall::OnDestroy()
{
	if (m_Ball != nullptr)
	{
		m_Ball->SetPocketHandler({});
		m_Ball->SetDefeatHandler({});
	}
	Uninit();
}

void PlayerBall::Init()
{
	// ステータス設定
	BallStatus status;
	status.attack = 1;     // 攻撃力

	SetStatus(status);
	GamePlayerState::ApplyTo(this);

	// モデルの読み込み
	LoadModel("assets/model/GolfBall/golf_ball.obj", "assets/model/GolfBall");
	GamePlayerState::ApplyTo(this);

	m_Ball->SetPosition(Vector3(0.0f, 1.0f, 0.0f));

	SetInitialPosition(m_Ball->GetPosition());

	//スケールを調整
	const float visualScale = m_Ball->GetStatus().radius > 0.0f
		? m_Ball->GetStatus().radius
		: 2.4f;
	m_Ball->SetScale(Vector3(
		visualScale,
		visualScale,
		visualScale));
	UpdateRadius();

	// Groundから台の高さを取得して合わせる
	std::vector<Ground*> grounds =
		GameRuntime::World().GetComponents<Ground>();
	if (grounds.size() > 0)
	{
		Vector3 position = m_Ball->GetPosition();
		position.y = grounds[0]->GetFieldHeight();
		m_Ball->SetPosition(position);
	}
	else
	{
		Vector3 position = m_Ball->GetPosition();
		position.y = 1.0f;
		m_Ball->SetPosition(position);
	}

	//最初に速度を与える
	//m_Ball->GetMutableVelocity().x = 1.0f;
	m_Ball->GetMutableVelocity() = Vector3::Zero;

	// 軌跡の初期位置を今のボールの位置にする
	m_LastTrailPos = m_Ball->GetPosition();
	m_TrajectoryPositions.clear();

	// 弾道予測用モデルの初期化（別途）
	InitTrajectoryVisualModel();
}

void PlayerBall::Update()
{
	if (IsDefeated()) return;    // 倒されている場合は更新しない
	RefreshNuisanceDebuffs();
	// Sample held debug keys once; fixed ticks apply the sampled acceleration.
	m_DebugMoveInput = Vector3::Zero;
	const bool keyboardCaptured = ImGui::GetCurrentContext() != nullptr &&
		ImGui::GetIO().WantCaptureKeyboard;
	if (GameBattleView::AcceptsDebugMovement() && !keyboardCaptured &&
		GetForegroundWindow() == Application::GetWindow())
	{
		if (Input::GetKeyPress(VK_W)) m_DebugMoveInput.z += 4.0f;
		if (Input::GetKeyPress(VK_S)) m_DebugMoveInput.z -= 4.0f;
		if (Input::GetKeyPress(VK_A)) m_DebugMoveInput.x -= 4.0f;
		if (Input::GetKeyPress(VK_D)) m_DebugMoveInput.x += 4.0f;
	}
	if (m_State == State::Idle && GameBattleView::AcceptsHumanInput())
	{
		UpdateAim();
	}
}

void PlayerBall::FixedUpdate()
{
	if (IsDefeated() || m_IsPocketed) return;
	m_CurrentFrame++;           // 軌跡の寿命計算で使うフレームを進める
	if (m_State == State::Simulation && !m_SkipSimulationOnNextFixedUpdate)
	{
		UpdateSimulation();
	}
    else if (!m_SkipSimulationOnNextFixedUpdate && m_Ball->HasPierceAbility() &&
		GameBattleView::State() == BattleState::BallsMoving)
    {
        // Another moving enemy can enter a piercer that has already gone idle.
        UpdateStopByFriction();
    }
	m_SkipSimulationOnNextFixedUpdate = false;
	UpdateTrailLife();           // 軌跡の寿命とフェードを更新する
	// Movement is performed for all balls together by BallPhysicsWorld.
}

void PlayerBall::Draw(Camera* cam)
{
	if (GameBattleView::ClearRewardActive()) return;
	if (m_RenderComponent == nullptr && GetGameObject() != nullptr)
		m_RenderComponent = GetGameObject()->GetComponent<BallRenderComponent>();
	if (IsDefeated() || cam == nullptr || m_Ball == nullptr ||
		m_RenderComponent == nullptr)
	{
		return;
	}

	//カメラを選択する
	cam->SetCamera();

	m_RenderComponent->BeginDraw();
	const Vector3 ballScale = m_Ball->GetScale();

	for (const auto& trailPoint : m_TrajectoryPositions)
	{
		const auto& pos = trailPoint.position;    //座標情報を取り出す

		const float fade = trailPoint.lifeRatio * trailPoint.lifeRatio;

		// フェードアウトに合わせてスケールを変化させる
		// 軌跡のスケールをfadeに比例させて、消える直前に小さくする
		const float baseScale = 0.48f + trailPoint.intensity * 0.32f;
		float currentScale = baseScale * fade;

		// 最小スケールを設ける
		currentScale = max(0.01f, currentScale);

		// 軌跡は少し小さくする
		Matrix r = Matrix::Identity;    // 回転なし
		Matrix t = Matrix::CreateTranslation(pos);
		Matrix s = Matrix::CreateScale(
			ballScale.x * currentScale,
			ballScale.y * currentScale,
			ballScale.z * currentScale);

		Matrix worldmtx = s * r * t;
		DrawMesh(worldmtx);
	}

	// 本来の向き（移動方向などを表す回転）
	Matrix rDirection = Matrix::CreateFromQuaternion(m_Ball->GetRotation());

	// 転がりの回転
	Matrix rRolling = Matrix::CreateFromQuaternion(m_Ball->GetMutableRollingRotation());

	// 3. 行列の合成：転がり(rRolling)を適用した後に、本来の向き(rDirection)を合わせる
	Matrix r = rDirection * rRolling;
	Matrix t = Matrix::CreateTranslation(m_Ball->GetPosition());
	Matrix s = Matrix::CreateScale(ballScale);

	Matrix worldmtx = s * r * t;
	DrawMesh(worldmtx);

	// ★ 弾道予測線の描画（新しいメソッド）
	DrawTrajectoryLine();
	DrawPierceTraces();
}

void PlayerBall::Uninit()
{
	// 現時点では解放が必要な専用リソースはない
}

void PlayerBall::Defeat()
{
	m_Ball->Defeat();

	// プレイヤー専用：ゲームオーバーへ移行
	GameBattleEvents::PlayerDefeated();
}

void PlayerBall::OnPocketHit()
{
	if (m_Ball == nullptr || m_IsPocketed)
	{
		return;
	}

	const Vector3 pocketPosition = GetPosition();
	const int hpBefore = GetHP();
	const int pocketDamage = GamePlayerState::PocketDamageAmount();
	const int hpDamage = GamePlayerState::AbsorbShieldDamage(pocketDamage);
	const int hpAfter = (std::max)(0, hpBefore - hpDamage);
	m_Ball->SetHP(hpAfter);
	GameBattleEvents::PlayerDamage(
		"pocket",
		(std::max)(0, hpBefore - hpAfter),
		std::string(),
		hpBefore,
		hpAfter);
	GameBattleEvents::PlayerPocketFeedback(
		pocketPosition, (std::max)(0, hpBefore - hpAfter));
	GamePlayerState::CaptureFrom(this);
	if (hpAfter <= 0)
	{
		m_Ball->Defeat();
		return;
	}

	EnterPocketQueue();
}

void PlayerBall::EnterPocketQueue()
{
	if (m_Ball == nullptr || IsDefeated() || m_IsPocketed)
	{
		return;
	}

	m_IsPocketed = true;
	m_Ball->ResetAtPosition(Vector3(0.0f, -1000.0f, 0.0f));
	m_State = State::Idle;
	if (GameObject* owner = GetGameObject())
	{
		owner->SetActive(false);
	}
}

void PlayerBall::ReturnFromPocket(const Vector3& position)
{
	if (m_Ball == nullptr || IsDefeated() || !m_IsPocketed)
	{
		return;
	}

	m_Ball->ResetAtPosition(position);
	m_IsPocketed = false;
	if (GameObject* owner = GetGameObject())
	{
		owner->SetActive(true);
	}

	m_TrajectoryPositions.clear();
	m_PrePositions.clear();
	m_LastTrailPos = m_Ball->GetPosition();
	m_StopCount = 0;
	m_Ball->GetMutableRollingRotation() = DirectX::SimpleMath::Quaternion::Identity;
	m_State = State::Idle;
}

void PlayerBall::TakeDamage(int damage)
{
	const int finalDamage = CalculateIncomingDamage(damage);
	const int hpDamage = GamePlayerState::AbsorbShieldDamage(finalDamage);
	if (hpDamage > 0)
	{
		m_Ball->DamageAfterDefense(hpDamage);
	}
	GamePlayerState::CaptureFrom(this);
}

void PlayerBall::RefreshNuisanceDebuffs()
{
	if (m_Ball == nullptr) return;
	StatusEffectCollection aura;
	for (const NuisanceBall* nuisance :
		GameRuntime::World().GetComponents<NuisanceBall>())
	{
		if (nuisance == nullptr || nuisance->GetGameObject() == nullptr ||
			!nuisance->GetGameObject()->IsActive() ||
			nuisance->GetGameObject()->IsDestroyRequested())
		{
			continue;
		}
		for (const StatusEffectType type : AllStatusEffectTypes)
		{
			const int total = aura.GetMagnitude(type) +
				nuisance->GetDebuffs().GetMagnitude(type);
			aura.Set(type, total);
		}
	}
	m_Ball->SetAuraStatusEffects(aura);
}

void PlayerBall::UpdateSimulation()
{
	UpdateDebugMove();          // デバッグ用のWASD加速を反映する
	UpdateStopByFriction();     // 摩擦による減速と停止判定を行う
	CheckFallRespawn();         // 落下していたらリスポーンする

	if (m_State == State::Simulation)
	{
		AddTrailPoint();         // まだ移動中なら軌跡を追加する
	}
}

void PlayerBall::UpdateDebugMove()
{
	const float moveSpeed = 0.01f;           // デバッグ移動の加速度
	Vector3 moveInput = m_DebugMoveInput;

	if (moveInput == Vector3::Zero) return;  // 入力がない場合は速度を変えない

	moveInput.Normalize();                   // 斜め移動が速くならないように正規化する
	m_Ball->GetMutableVelocity() += moveInput * moveSpeed;     // 入力方向へ速度を加算する
}

void PlayerBall::UpdateStopByFriction()
{
    Vector3 exitDirection;
    bool exitingPierce = false;
    if (m_Ball->HasPierceAbility())
    {
        const auto* collision = GetGameObject()->GetComponent<BallCollisionComponent>();
        exitingPierce = collision && collision->TryGetPierceExitDirection(exitDirection);
    }
    if (exitingPierce) m_State = State::Simulation;
    if (m_State != State::Simulation) return;
    if (BallPhysicsRules::PlayerFriction(m_Ball->GetMutableVelocity(), m_Ball->GetMutableAcceleration(),
        m_Ball->GetStatus(), m_Ball->GetMutableFriction(), m_StopCount,
        exitingPierce ? &exitDirection : nullptr)) m_State = State::Idle;
}

void PlayerBall::CheckFallRespawn()
{
	if (m_Ball->GetPosition().y >= -100.0f) return;       // 一定以下に落ちていなければ何もしない

	m_Ball->SetPosition(Vector3(0.0f, 50.0f, 0.0f));      // リスポーン位置へ戻す
	m_Ball->GetMutableVelocity() = Vector3::Zero;                          // 落下時の速度を消す
	m_TrajectoryPositions.clear();                       // 落下前の軌跡を消す
}

void PlayerBall::AddTrailPoint()
{
	Vector3 planarVelocity = m_Ball->GetVelocity();
	planarVelocity.y = 0.0f;
	const float speed = planarVelocity.Length();
	const float intensity = std::clamp(speed / 8.0f, 0.28f, 1.0f);
	const float stepSize = 2.1f - intensity * 0.8f;

	Vector3 toCurrent = m_Ball->GetPosition() - m_LastTrailPos;
	float dist = toCurrent.Length();              // 最後の軌跡点から現在位置までの距離

	if (dist < stepSize) return;                         // 間隔未満なら軌跡は追加しない

	toCurrent.Normalize();                               // 軌跡を等間隔で置くため移動方向だけにする

	while (dist >= stepSize)
	{
		m_LastTrailPos += toCurrent * stepSize;
		m_TrajectoryPositions.push_back({
			m_LastTrailPos,
			m_CurrentFrame,
			1.0f,
			intensity });
		dist -= stepSize;
	}
	constexpr std::size_t maximumTrailPoints = 144;
	if (m_TrajectoryPositions.size() > maximumTrailPoints)
	{
		m_TrajectoryPositions.erase(
			m_TrajectoryPositions.begin(),
			m_TrajectoryPositions.begin() +
				(m_TrajectoryPositions.size() - maximumTrailPoints));
	}
}

void PlayerBall::UpdateTrailLife()
{
	const int expirationFrame = m_CurrentFrame - TRAIL_DURATION_FRAMES;

	while (!m_TrajectoryPositions.empty() &&
		m_TrajectoryPositions.front().timestamp < expirationFrame)
	{
		m_TrajectoryPositions.erase(m_TrajectoryPositions.begin());
	}

	for (auto& point : m_TrajectoryPositions)
	{
		const int remainingFrames =
			TRAIL_DURATION_FRAMES - (m_CurrentFrame - point.timestamp);

		point.lifeRatio =
			(max)(0.0f, static_cast<float>(remainingFrames) / TRAIL_DURATION_FRAMES);
	}
}

void PlayerBall::FireAutomatedShot(
	const DirectX::SimpleMath::Vector3& velocity)
{
	m_SkipSimulationOnNextFixedUpdate = false;
	Shot(velocity);
	m_IsPowerDragging = false;
	m_State = State::Simulation;
	m_TrajectoryPositions.clear();
	m_PrePositions.clear();
	m_PreTrajectoryDirty = true;
	m_StopCount = 0;

	GameShotPrediction::WriteVerification(
		*this, GetVelocity(), m_SkipSimulationOnNextFixedUpdate);
	GameBattleEvents::PlayerShotFired(this);
}

Vector3 PlayerBall::GetShotVector() const
{
	// TC-18: m_AimAngle=0, m_ShotPower=5 → Vector3(sin(0),0,cos(0))×5 = Vector3(0,0,5)
	if (m_IsPowerDragging)
	{
		return m_LockedShotDirection * m_ShotPower;
	}

	return Vector3(sin(m_AimAngle), 0.0f, cos(m_AimAngle)) * m_ShotPower;
}

void PlayerBall::DrawImGui()
{
	// 親クラスの共通UIを呼ぶ
	m_Ball->DrawImGui("PlayerBall");

	// PlayerBall固有の情報を追加
	if (ImGui::CollapsingHeader("PlayerBall Detail"))
	{
		// ▼ プレイヤーボール詳細ヘッダー内の既存表示の後に追加 ▼

		// エイム情報
		ImGui::SliderFloat("Aim Angle", &m_AimAngle, -3.14159265f, 3.14159265f);
		ImGui::SliderFloat("Shot Power", &m_ShotPower, m_MinShotPower, m_MaxShotPower);

		// 現在の GameState 表示
		const BattleState battleState = GameBattleView::State();

		ImGui::Text(
			"BattleState: %s",
			ToString(battleState));

		ImGui::Text(
			"ClearReward: %s",
			GameBattleView::ClearRewardActive()
			? "true"
			: "false");

		// 状態表示
		const char* stateStr = "";
		switch (m_State)
		{
		case State::Simulation: stateStr = "Simulation"; break;
		case State::Idle:       stateStr = "Idle";       break;
		case State::Goal:       stateStr = "Goal";       break;
		case State::Dead:       stateStr = "Dead";       break;
		}
		ImGui::Text("State: %s", stateStr);

		// 停止カウント
		ImGui::Text("StopCount: %d", m_StopCount);
	}
}

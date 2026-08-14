#include "PlayerBall.h"
#include "Game.h"
#include "Ground.h"
#include "TableFrame.h"
#include "Camera.h"
#include "Application.h"
#include "imgui/imgui.h"
#include "GameObject.h"

#include <algorithm>
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
	Draw(Game::GetCamera());
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
	status.maxHp = 10;    // 最大HP
	status.attack = 1;     // 攻撃力
	status.defense = 0;     // 防御力

	SetStatus(status);
	Game::GetInstance()->ApplyPlayerStatusTo(this);

	// モデルの読み込み
	LoadModel("assets/model/GolfBall/golf_ball.obj", "assets/model/GolfBall");

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

	// ★ Groundから台の高さを取得して合わせる
	std::vector<Ground*> grounds = Game::GetInstance()->GetComponents<Ground>();
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

	// デフォルトモデルを設定
	m_TrajectoryModel = std::make_unique<BallTrajectoryModel>();

	// 弾道予測用モデルの初期化（別途）
	InitTrajectoryVisualModel();
}

void PlayerBall::Update()
{
	if (IsDefeated()) return;    // 倒されている場合は更新しない

	m_CurrentFrame++;           // 軌跡の寿命計算で使うフレームを進める

	// 現在の状態に応じて、更新処理を切り替える
	switch (m_State)
	{
	case State::Simulation:
		UpdateSimulation();
		break;

	case State::Idle:
		if (!Game::GetInstance()->IsBalanceAutoPlayEnabled())
		{
			UpdateAim();
		}
		break;
	}

	UpdateTrailLife();           // 軌跡の寿命とフェードを更新する
	UpdatePhysics();             // BallComponent側の物理更新を行う
}

void PlayerBall::Draw(Camera* cam)
{
	if (IsDefeated())
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

		float fade = trailPoint.lifeRatio;

		// フェードアウトに合わせてスケールを変化させる
		// 軌跡のスケールをfadeに比例させて、消える直前に小さくする
		float baseScale = 0.8f;
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
}

void PlayerBall::Uninit()
{
	// 現時点では解放が必要な専用リソースはない
}

void PlayerBall::Defeat()
{
	m_Ball->Defeat();

	// プレイヤー専用：ゲームオーバーへ移行
	Game::GetInstance()->SetGameState(GameState::GameOver);
}

void PlayerBall::OnPocketHit()
{
	const int hpBefore = GetHP();
	const int pocketDamage =
		Game::GetInstance()->GetPlayerPocketDamageAmount();
	const int hpAfter = (std::max)(0, hpBefore - pocketDamage);
	m_Ball->SetHP(hpAfter);
	Game::GetInstance()->NotifyPlayerDamage(
		"pocket",
		(std::max)(0, hpBefore - hpAfter));
	Game::GetInstance()->CapturePlayerStatusFrom(this);
	if (hpAfter <= 0)
	{
		m_Ball->Defeat();
		return;
	}

	const Vector3 returnPosition =
		Game::GetInstance()->FindPlayerPocketReturnPosition(this);
	m_Ball->ResetAtPosition(returnPosition);

	m_TrajectoryPositions.clear();
	m_PrePositions.clear();
	m_LastTrailPos = m_Ball->GetPosition();
	m_StopCount = 0;
	m_Ball->GetMutableRollingRotation() = DirectX::SimpleMath::Quaternion::Identity;
	m_State = State::Idle;
}

void PlayerBall::TakeDamage(int damage)
{
	Damage(damage);    // BallComponent側のダメージ処理を呼ぶ
	Game::GetInstance()->CapturePlayerStatusFrom(this);
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
	Vector3 moveInput = Vector3::Zero;   // WASD入力から作る移動方向

	if (Input::GetKeyPress(VK_W)) moveInput.z += 4.0f;
	if (Input::GetKeyPress(VK_S)) moveInput.z -= 4.0f;
	if (Input::GetKeyPress(VK_A)) moveInput.x -= 4.0f;
	if (Input::GetKeyPress(VK_D)) moveInput.x += 4.0f;

	if (moveInput == Vector3::Zero) return;  // 入力がない場合は速度を変えない

	moveInput.Normalize();                   // 斜め移動が速くならないように正規化する
	m_Ball->GetMutableVelocity() += moveInput * moveSpeed;     // 入力方向へ速度を加算する
}

void PlayerBall::UpdateStopByFriction()
{
	if (m_Ball->GetMutableVelocity().LengthSquared() < 0.03f)
	{
		m_StopCount++;                         // ほぼ停止しているフレーム数を数える
	}
	else
	{
		m_StopCount = 0;                       // 動いている場合は停止カウントをリセットする

		Vector3 deceleration = -m_Ball->GetMutableVelocity();    // 速度と逆方向に減速させる
		deceleration.Normalize();             // 摩擦方向だけを使うため正規化する

		m_Ball->GetMutableAcceleration() = deceleration * m_Ball->GetMutableFriction();
		m_Ball->GetMutableVelocity() += m_Ball->GetMutableAcceleration();
	}

	if (m_StopCount > 10)
	{
		m_Ball->GetMutableVelocity() = Vector3::Zero;            // 完全停止として速度を0にする
		m_State = State::Idle;                 // ショット待ち状態へ戻す
	}
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
	const float stepSize = 1.5f;                         // 軌跡を追加する間隔

	Vector3 toCurrent = m_Ball->GetPosition() - m_LastTrailPos;
	float dist = toCurrent.Length();              // 最後の軌跡点から現在位置までの距離

	if (dist < stepSize) return;                         // 間隔未満なら軌跡は追加しない

	toCurrent.Normalize();                               // 軌跡を等間隔で置くため移動方向だけにする

	while (dist >= stepSize)
	{
		m_LastTrailPos += toCurrent * stepSize;
		m_TrajectoryPositions.push_back({ m_LastTrailPos, m_CurrentFrame, 1.0f });
		dist -= stepSize;
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

void PlayerBall::UpdateAim()
{
	GameState gameState = Game::GetInstance()->GetGameState();

	if (gameState != GameState::AimingDirection &&
		gameState != GameState::AimingPower &&
		gameState != GameState::ConfirmShot)
	{
		return;
	}

	const bool imguiWantsMouse =
		ImGui::GetCurrentContext() != nullptr &&
		ImGui::GetIO().WantCaptureMouse;

	const bool leftPressed =
		Input::GetKeyTrigger(VK_LBUTTON) && !imguiWantsMouse;
	const bool leftReleased =
		Input::GetKeyRelease(VK_LBUTTON);
	const bool rightPressed =
		Input::GetKeyTrigger(VK_RBUTTON) && (!imguiWantsMouse || m_IsPowerDragging);

	if (m_IsPowerDragging)
	{
		UpdateShotPowerFromMouseDrag();

		if (rightPressed)
		{
			CancelMousePowerDrag();
		}
		else if (leftReleased)
		{
			UpdateShotPowerFromMouseDrag();
			FireMouseShot();
			return;
		}
	}
	else
	{
		if (gameState != GameState::AimingDirection)
		{
			Game::GetInstance()->SetGameState(GameState::AimingDirection);
		}

		UpdateAimDirectionFromMouse();

		if (leftPressed)
		{
			//UpdateAimDirectionFromMouse();
			BeginMousePowerDrag();
		}
		else if (rightPressed)
		{
			CancelMousePowerDrag();
		}
	}

	bool previewChanged =
		fabs(m_AimAngle - m_LastPreviewAimAngle) > 0.001f ||
		fabs(m_ShotPower - m_LastPreviewShotPower) > 0.001f ||
		(m_Ball->GetPosition() - m_LastPreviewPosition).LengthSquared() > 0.01f;

	if (m_PreTrajectoryDirty || previewChanged || m_PrePositions.empty())
	{
		GeneratePreTrajectory(GetShotVector());

		m_LastPreviewAimAngle = m_AimAngle;
		m_LastPreviewShotPower = m_ShotPower;
		m_LastPreviewPosition = m_Ball->GetPosition();
		m_PreTrajectoryDirty = false;
	}
}

bool PlayerBall::TryGetMouseAimPosition(Vector3& aimPosition) const
{
	// ウィンドウハンドルを取得する
	HWND hwnd = Application::GetWindow();
	if (hwnd == nullptr)
	{
		return false;
	}

	// ウィンドウ内の描画を取得する
	RECT clientRect{};
	if (!GetClientRect(hwnd, &clientRect))
	{
		return false;
	}

	// ウィンドウのクライアント領域の幅と高さを計算する
	float clientWidth = static_cast<float>(clientRect.right - clientRect.left);
	float clientHeight = static_cast<float>(clientRect.bottom - clientRect.top);
	if (clientWidth <= 0.0f || clientHeight <= 0.0f)
	{
		return false;
	}

	// ビューポートの計算（アスペクト比を維持するために黒帯を考慮）//
	float viewportX = 0.0f;
	float viewportY = 0.0f;
	float viewportWidth = clientWidth;
	float viewportHeight = clientHeight;

	// ゲーム側が考慮する画面比
	const float targetAspect = static_cast<float>(Application::GetWidth()) /
		static_cast<float>(Application::GetHeight());

	// 実際のウィンドウ比
	const float windowAspect = clientWidth / clientHeight;

	// アスペクト比を維持するために黒帯を考慮してビューポートを計算（黒帯でにマウスを向けても狙いがずれない）
	if (windowAspect > targetAspect)
	{
		viewportHeight = clientHeight;
		viewportWidth = clientHeight * targetAspect;
		viewportX = (clientWidth - viewportWidth) * 0.5f;
	}
	else
	{
		viewportWidth = clientWidth;
		viewportHeight = clientWidth / targetAspect;
		viewportY = (clientHeight - viewportHeight) * 0.5f;
	}

	// マウス位置をビューポート内に制限する（黒帯外のマウス位置は端に固定）
	DirectX::XMFLOAT2 mousePos = Input::GetMousePosition();    // まだ画面上の2D座標

	// マウス座標をビューポート内に制限する（画面外に行ったら、一番近い画面端に補正）
	const float viewportRight = viewportX + viewportWidth;
	const float viewportBottom = viewportY + viewportHeight;
	float mouseX = mousePos.x < viewportX ? viewportX : (mousePos.x > viewportRight ? viewportRight : mousePos.x);
	float mouseY = mousePos.y < viewportY ? viewportY : (mousePos.y > viewportBottom ? viewportBottom : mousePos.y);

	// プロジェクション行列を取得（視野角、アスペクト比、ニア・ファー平面の設定）
	constexpr float fieldOfView = DirectX::XMConvertToRadians(45.0f);
	constexpr float nearPlane = 1.0f;
	constexpr float farPlane = 1000.0f;
	DirectX::XMMATRIX projection = DirectX::XMMatrixPerspectiveFovLH(
		fieldOfView,
		targetAspect,
		nearPlane,
		farPlane);

	// ビュー行列を取得（カメラの位置と向き）
	Matrix viewMatrix = Camera::GetInstance().GetViewMatrix();
	DirectX::XMMATRIX view = viewMatrix;

	// マウス位置をワールド座標に変換するために、ニア平面とファー平面の2点を取得する //

	// ニア平面の座標を取得
	DirectX::XMVECTOR nearPointVector = DirectX::XMVector3Unproject(
		DirectX::XMVectorSet(mouseX, mouseY, 0.0f, 1.0f),
		viewportX,
		viewportY,
		viewportWidth,
		viewportHeight,
		0.0f,
		1.0f,
		projection,
		view,
		DirectX::XMMatrixIdentity());

	// ファー平面の座標を取得
	DirectX::XMVECTOR farPointVector = DirectX::XMVector3Unproject(
		DirectX::XMVectorSet(mouseX, mouseY, 1.0f, 1.0f),
		viewportX,
		viewportY,
		viewportWidth,
		viewportHeight,
		0.0f,
		1.0f,
		projection,
		view,
		DirectX::XMMatrixIdentity());

	// XMVECTORを、DirectX::SimpleMath::Vector3に変換する
	DirectX::XMFLOAT3 nearFloat{};
	DirectX::XMFLOAT3 farFloat{};
	DirectX::XMStoreFloat3(&nearFloat, nearPointVector);
	DirectX::XMStoreFloat3(&farFloat, farPointVector);

	Vector3 nearPoint(nearFloat.x, nearFloat.y, nearFloat.z);
	Vector3 farPoint(farFloat.x, farFloat.y, farFloat.z);

	// ニア平面とファー平面の2点から、マウス位置を通るレイを作る
	Vector3 ray = farPoint - nearPoint;

	// レイのy成分がほぼ0の場合は、床面との交点が計算できないので失敗とする
	if (std::fabs(ray.y) <= 0.0001f)
	{
		return false;
	}

	// レイのパラメータtを計算して、床面（y=ボールの高さ）との交点を求める
	const Vector3 ballPosition = m_Ball->GetPosition();
	float t = (ballPosition.y - nearPoint.y) / ray.y;

	// tが負の場合は、レイが床面の下方向を向いているので失敗とする
	if (t < 0.0f)
	{
		return false;
	}

	// 床面の交点を計算して、aimPositionに格納する
	aimPosition = nearPoint + ray * t;
	aimPosition.y = ballPosition.y;
	return true;
}

void PlayerBall::UpdateAimDirectionFromMouse()
{
	Vector3 aimPosition;                         // マウスが指している床上の座標
	if (!TryGetMouseAimPosition(aimPosition))
	{
		return;
	}

	Vector3 aimVector = aimPosition - m_Ball->GetPosition();
	aimVector.y = 0.0f;                           // 水平方向だけで角度を決める
	if (aimVector.LengthSquared() <= 0.0001f)
	{
		return;
	}

	m_AimAngle = static_cast<float>(std::atan2(aimVector.x, aimVector.z));
}

void PlayerBall::BeginMousePowerDrag()
{
	m_IsPowerDragging = true;                                             // パワードラッグ中にする
	m_LockedAimAngle = m_AimAngle;                                        // クリック時点の角度を固定する
	m_LockedShotDirection = Vector3(sin(m_LockedAimAngle), 0.0f, cos(m_LockedAimAngle));
	m_LockedShotDirection.Normalize();                                    // ショット方向として使えるよう正規化する
	m_PowerDragStartMousePos = Input::GetMousePosition();                 // パワー計算の基準位置を保存する
	m_ShotPower = m_MinShotPower;                                         // ドラッグ開始時は最小パワーにする
	m_PreTrajectoryDirty = true;                                          // 予測線を再計算対象にする
	Game::GetInstance()->SetGameState(GameState::AimingPower);
}

void PlayerBall::UpdateShotPowerFromMouseDrag()
{
	m_AimAngle = m_LockedAimAngle;                                        // ドラッグ中は方向を固定する

	DirectX::XMFLOAT2 mousePos = Input::GetMousePosition();               // 現在のマウス座標を取得する

	const float dx = mousePos.x - m_PowerDragStartMousePos.x;             // ドラッグ開始位置からのX差分
	const float dy = mousePos.y - m_PowerDragStartMousePos.y;             // ドラッグ開始位置からのY差分
	const float dragDistance = std::sqrt(dx * dx + dy * dy);              // ドラッグ距離を計算する

	const float powerRatio =
		std::clamp(dragDistance / m_PixelsForMaxShotPower, 0.0f, 1.0f);    // 距離を0.0〜1.0の割合にする

	const float rawPower =
		m_MinShotPower + (m_MaxShotPower - m_MinShotPower) * powerRatio;   // 割合からパワー値を計算する

	const float steppedPower =
		std::floor(rawPower / m_PowerPreviewStep + 0.5f) * m_PowerPreviewStep; // 予測線の揺れを抑えるため丸める

	m_ShotPower = std::clamp(steppedPower, m_MinShotPower, m_MaxShotPower);
}

void PlayerBall::CancelMousePowerDrag()
{
	m_IsPowerDragging = false;                                      // パワードラッグを終了する
	m_PreTrajectoryDirty = true;                                    // 方向合わせに戻るため予測線を更新対象にする
	Game::GetInstance()->SetGameState(GameState::AimingDirection);  // 方向合わせ状態へ戻す
}

void PlayerBall::FireMouseShot()
{
	m_AimAngle = m_LockedAimAngle;                                  // 固定していた角度を現在角度に反映する
	Shot(GetShotVector());                                          // 固定方向と現在パワーで速度を設定する
	m_IsPowerDragging = false;                                      // パワードラッグを終了する
	m_State = State::Simulation;                                    // 物理演算中に切り替える
	m_PrePositions.clear();                                         // ショット開始後は予測線を消す
	m_PreTrajectoryDirty = true;                                    // 次回停止後に予測線を再計算できるようにする
	m_StopCount = 0;                                                // 停止判定カウントをリセットする
	Game::GetInstance()->OnPlayerShotFired(this);
	Game::GetInstance()->SetGameState(GameState::BallsMoving);
}

void PlayerBall::FireAutomatedShot(
	const DirectX::SimpleMath::Vector3& velocity)
{
	Shot(velocity);
	m_IsPowerDragging = false;
	m_State = State::Simulation;
	m_TrajectoryPositions.clear();
	m_PrePositions.clear();
	m_PreTrajectoryDirty = true;
	m_StopCount = 0;

	Game::GetInstance()->OnPlayerShotFired(this);
	Game::GetInstance()->SetGameState(GameState::BallsMoving);
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

void PlayerBall::GeneratePreTrajectory(const DirectX::SimpleMath::Vector3& initialVelocity)
{
	m_PrePositions.clear();
	m_PreviewHitBall = false;
	m_PreviewGhostBallPosition = Vector3::Zero;
	m_PreviewHitBallPosition = Vector3::Zero;
	m_PreviewObjectBallDirection = Vector3::Zero;

	const int PREDICTION_FRAMES = 2000;
	const size_t MAX_PREVIEW_POINTS = 1000;
	const float PREVIEW_POINT_INTERVAL = 1.0f;
	const float PREVIEW_BALL_HIT_SCALE = 1.0f;
	const float PREVIEW_SIM_SPEED = m_MaxShotPower;

	m_PrePositions.reserve(MAX_PREVIEW_POINTS);

	if (!m_TrajectoryModel)
	{
		m_TrajectoryModel = std::make_unique<BallTrajectoryModel>();
	}

	Vector3 simPosition = m_Ball->GetPosition();
	Vector3 simVelocity = initialVelocity;
	Vector3 simAcceleration;

	std::vector<Collision::Segment> walls;
	std::vector<TableFrame*> frames = Game::GetInstance()->GetComponents<TableFrame>();

	float fieldHeight = m_Ball->GetPosition().y;

	for (TableFrame* frame : frames)
	{
		std::vector<Collision::Segment> frameWalls = frame->GetWalls();

		walls.insert(
			walls.end(),
			frameWalls.begin(),
			frameWalls.end());
	}

	simPosition.y = fieldHeight;
	simVelocity.y = 0.0f;

	if (simVelocity.LengthSquared() <= 0.0001f)
	{
		return;
	}

	simVelocity.Normalize();
	simVelocity *= PREVIEW_SIM_SPEED;

	m_PrePositions.push_back({ simPosition, 0, 1.0f });

	std::vector<BallComponent*> balls = Game::GetInstance()->GetComponents<BallComponent>();
	bool previewPierceAvailable = m_Ball->HasPierceAbility();
	BallComponent* previewPiercedBall = nullptr;

	for (int frame = 0; frame < PREDICTION_FRAMES; ++frame)
	{
		Vector3 oldPosition = simPosition;

		simVelocity.y = 0.0f;
		m_TrajectoryModel->SimulateStep(simPosition, simVelocity, simAcceleration);
		simPosition.y = fieldHeight;
		simVelocity.y = 0.0f;

		Vector3 frameMove = simPosition - oldPosition;
		frameMove.y = 0.0f;

		float maxStep = m_Ball->GetRadius() * 0.5f;
		int subSteps = max(1, (int)ceil(frameMove.Length() / maxStep));
		Vector3 stepMove = frameMove / (float)subSteps;

		simPosition = oldPosition;

		bool hit = false;

		for (int step = 0; step < subSteps; ++step)
		{
			simPosition += stepMove;
			simPosition.y = fieldHeight;

			for (const auto& wall : walls)
			{
				Vector3 contactPoint;
				float distance = Collision::DistancePointToSegment(simPosition, wall, contactPoint);

				if (distance <= m_Ball->GetRadius())
				{
					Vector3 normal = simPosition - contactPoint;
					normal.y = 0.0f;

					if (normal.LengthSquared() > 0.0001f)
					{
						normal.Normalize();
					}
					else
					{
						Vector3 wallVec = wall.end - wall.start;
						wallVec.Normalize();
						normal = Vector3(-wallVec.z, 0.0f, wallVec.x);
					}

					if (Collision::Dot(stepMove, normal) > 0.0f)
					{
						normal = -normal;
					}

					Vector3 visualContactPoint = contactPoint;
					Vector3 lineStart = m_PrePositions.back().position;
					Vector3 lineDir = stepMove;
					Vector3 wallDir = wall.end - wall.start;
					lineStart.y = 0.0f;
					lineDir.y = 0.0f;
					wallDir.y = 0.0f;

					float cross = lineDir.x * wallDir.z - lineDir.z * wallDir.x;
					if (fabs(cross) > 0.0001f)
					{
						Vector3 toWall = wall.start - lineStart;
						toWall.y = 0.0f;

						float t = (toWall.x * wallDir.z - toWall.z * wallDir.x) / cross;
						if (t >= 0.0f)
						{
							visualContactPoint = lineStart + lineDir * t;
						}
					}
					visualContactPoint.y = fieldHeight;

					simPosition = contactPoint + normal * m_Ball->GetRadius();
					simPosition.y = fieldHeight;
					m_PrePositions.push_back({ visualContactPoint, 0, 1.0f });
					hit = true;
					break;
				}
			}

			if (hit) break;

			// 対象ボールとの接触点を安定させるため、移動線分と拡張円の交点で判定する。
			for (BallComponent* other : balls)
			{
				if (other == m_Ball) continue;
				if (other == previewPiercedBall) continue;
				if (other->IsDefeated()) continue;

				Collision::Sphere otherSphere = other->GetSphere();
				otherSphere.center.y = fieldHeight;

				float myHitRadius = m_Ball->GetRadius() * PREVIEW_BALL_HIT_SCALE;
				float otherHitRadius = otherSphere.radius * PREVIEW_BALL_HIT_SCALE;
				float minDist = myHitRadius + otherHitRadius;

				Vector3 segmentStart = simPosition - stepMove;
				Vector3 segmentEnd = simPosition;
				segmentStart.y = fieldHeight;
				segmentEnd.y = fieldHeight;

				Vector3 segmentMove = segmentEnd - segmentStart;
				segmentMove.y = 0.0f;

				float a = segmentMove.LengthSquared();
				if (a <= 0.0001f)
				{
					continue;
				}

				Vector3 toStart = segmentStart - otherSphere.center;
				toStart.y = 0.0f;

				float b = 2.0f * (toStart.x * segmentMove.x + toStart.z * segmentMove.z);
				float c = toStart.LengthSquared() - minDist * minDist;
				float hitT = -1.0f;

				if (c <= 0.0f)
				{
					hitT = 0.0f;
				}
				else
				{
					float discriminant = b * b - 4.0f * a * c;
					if (discriminant >= 0.0f)
					{
						hitT = (-b - std::sqrt(discriminant)) / (2.0f * a);
					}
				}

				if (hitT >= 0.0f && hitT <= 1.0f)
				{
					Vector3 hitCenter = segmentStart + segmentMove * hitT;
					hitCenter.y = fieldHeight;

					if (previewPierceAvailable)
					{
						constexpr float PierceSpeedRetention = 0.75f;
						previewPierceAvailable = false;
						previewPiercedBall = other;
						simVelocity *= PierceSpeedRetention;
						stepMove *= PierceSpeedRetention;
						m_PrePositions.push_back(
							{ hitCenter, 0, 1.0f });
						break;
					}

					Vector3 normal = hitCenter - otherSphere.center;
					normal.y = 0.0f;

					if (normal.LengthSquared() > 0.0001f)
					{
						normal.Normalize();
					}
					else
					{
						normal = -segmentMove;
						normal.y = 0.0f;

						if (normal.LengthSquared() > 0.0001f)
							normal.Normalize();
						else
							normal = Vector3::UnitZ;
					}

					float ghostDistance = m_Ball->GetRadius() + otherSphere.radius;
					m_PreviewHitBall = true;
					m_PreviewGhostBallPosition = otherSphere.center + normal * ghostDistance;
					m_PreviewGhostBallPosition.y = fieldHeight;
					m_PreviewHitBallPosition = otherSphere.center;
					m_PreviewHitBallPosition.y = fieldHeight;
					m_PreviewObjectBallDirection = -normal;

					simPosition = m_PreviewGhostBallPosition;
					simPosition.y = fieldHeight;
					m_PrePositions.push_back({ simPosition, 0, 1.0f });
					hit = true;
					break;
				}
			}

			if (hit) break;
		}

		if (hit)
		{
			break;
		}

		float distSq = (simPosition - m_PrePositions.back().position).LengthSquared();

		if (distSq > PREVIEW_POINT_INTERVAL * PREVIEW_POINT_INTERVAL)
		{
			m_PrePositions.push_back({ simPosition, 0, 1.0f });

			if (m_PrePositions.size() >= MAX_PREVIEW_POINTS)
			{
				break;
			}
		}
	}
}

void PlayerBall::InitTrajectoryVisualModel()
{
	// ★ PreviewMeshの初期化
	m_PreviewMesh.InitQuad();

	// ★ MeshRendererを初期化
	m_PreviewMeshRenderer.Init(m_PreviewMesh);

	auto addGuideMaterial = [this](const DirectX::SimpleMath::Color& color)
		{
			std::unique_ptr<Material> mat = std::make_unique<Material>();
			MATERIAL matData{};
			matData.Ambient = color;
			matData.Diffuse = color;
			matData.Specular = { 0.2f, 0.2f, 0.2f, 1.0f };
			matData.Emission = color;
			matData.Shiness = 4.0f;
			matData.TextureEnable = FALSE;
			mat->Create(matData);
			m_PreviewMaterials.push_back(std::move(mat));
		};

	addGuideMaterial({ 0.35f, 0.95f, 1.0f, 1.0f });   // 軌道予測線の色
	addGuideMaterial({ 1.0f, 1.0f, 1.0f, 0.9f });     // 接触時のプレイヤーボール半径の色
	addGuideMaterial({ 1.0f, 0.86f, 0.25f, 1.0f });   // 当たったボールが飛ぶ方向の色

	// サブセットを作成
	SUBSET subset;
	subset.MaterialIdx = 0;
	subset.IndexNum = static_cast<unsigned int>(m_PreviewMesh.GetIndices().size());
	subset.IndexBase = 0;
	subset.VertexBase = 0;
	m_PreviewSubsets.push_back(subset);
}

void PlayerBall::DrawTrajectoryLine()
{
	if (m_State == State::Simulation || m_PrePositions.size() < 2)
		return;

	// プレビュー用MeshRendererを準備
	Renderer::SetDepthEnable(true);
	m_PreviewMeshRenderer.BeforeDraw();

	const float Y_OFFSET = 0.8f;
	const float MAIN_LINE_THICKNESS = 0.32f;
	const float GHOST_LINE_THICKNESS = 0.2f;
	const float OBJECT_LINE_THICKNESS = 0.32f;
	const float OBJECT_LINE_LENGTH = 10.0f;

	for (size_t i = 0; i < m_PrePositions.size() - 1; i++)
	{
		Vector3 start = m_PrePositions[i].position;
		Vector3 end = m_PrePositions[i + 1].position;

		if (i == 0)
		{
			Vector3 lineDir = end - start;
			lineDir.y = 0.0f;

			float lineLength = lineDir.Length();
			float startOffset = m_Ball->GetRadius() + MAIN_LINE_THICKNESS;

			if (lineLength <= startOffset)
			{
				continue;
			}

			lineDir /= lineLength;
			start += lineDir * startOffset;
		}

		DrawGuideSegment(
			start,
			end,
			MAIN_LINE_THICKNESS,
			Y_OFFSET,
			0);
	}

	if (m_PreviewHitBall)
	{
		DrawGuideCircle(
			m_PreviewGhostBallPosition,
			m_Ball->GetRadius(),
			GHOST_LINE_THICKNESS,
			Y_OFFSET + 0.03f,
			1);

		if (m_PreviewObjectBallDirection.LengthSquared() > 0.0001f)
		{
			Vector3 objectLineEnd =
				m_PreviewHitBallPosition +
				m_PreviewObjectBallDirection * OBJECT_LINE_LENGTH;

			DrawGuideSegment(
				m_PreviewHitBallPosition,
				objectLineEnd,
				OBJECT_LINE_THICKNESS,
				Y_OFFSET + 0.06f,
				2);
		}
	}

	Renderer::SetDepthEnable(true);
}

void PlayerBall::DrawGuideSegment(
	const DirectX::SimpleMath::Vector3& start,
	const DirectX::SimpleMath::Vector3& end,
	float thickness,
	float yOffset,
	int materialIndex)
{
	if (materialIndex < 0 || materialIndex >= static_cast<int>(m_PreviewMaterials.size()))
	{
		return;
	}

	Vector3 startPos = start;                       // 描画開始位置
	Vector3 endPos = end;                         // 描画終了位置
	startPos.y += yOffset;                          // 床と重ならないように少し上げる
	endPos.y += yOffset;                            // 床と重ならないように少し上げる

	Vector3 forward = endPos - startPos;            // 線分の向き
	float distance = forward.Length();             // 線分の長さ
	if (distance <= 0.0001f)
	{
		return;
	}

	forward /= distance;                            // CreateWorldに渡すため方向だけにする

	Vector3 midPos = (startPos + endPos) * 0.5f;    // 線分の中央にメッシュを配置する
	Matrix s = Matrix::CreateScale(thickness, thickness, distance);
	Matrix rt = Matrix::CreateWorld(midPos, forward, Vector3::Up);
	Matrix worldmtx = s * rt;

	Renderer::SetWorldMatrix(&worldmtx);
	m_PreviewMaterials[materialIndex]->SetGPU();
	m_PreviewMeshRenderer.DrawSubset(
		m_PreviewSubsets[0].IndexNum,
		m_PreviewSubsets[0].IndexBase,
		m_PreviewSubsets[0].VertexBase);
}

void PlayerBall::DrawGuideCircle(
	const DirectX::SimpleMath::Vector3& center,
	float radius,
	float thickness,
	float yOffset,
	int materialIndex)
{
	const int segmentCount = 48;                    // 円を構成する線分数

	for (int i = 0; i < segmentCount; ++i)
	{
		float angle0 = DirectX::XM_2PI * static_cast<float>(i) / static_cast<float>(segmentCount);
		float angle1 = DirectX::XM_2PI * static_cast<float>(i + 1) / static_cast<float>(segmentCount);

		Vector3 p0 = center + Vector3(cos(angle0) * radius, 0.0f, sin(angle0) * radius);
		Vector3 p1 = center + Vector3(cos(angle1) * radius, 0.0f, sin(angle1) * radius);
		DrawGuideSegment(p0, p1, thickness, yOffset, materialIndex);
	}
}

void PlayerBall::DrawImGui()
{
	// 親クラスの共通UIを呼ぶ
	m_Ball->DrawImGui("PlayerBall");

	// PlayerBall固有の情報を追加
	if (ImGui::CollapsingHeader("PlayerBall Detail"))
	{
		// ▼ "PlayerBall Detail" ヘッダー内の既存表示の後に追加 ▼

		// エイム情報
		ImGui::SliderFloat("Aim Angle", &m_AimAngle, -3.14159265f, 3.14159265f);
		ImGui::SliderFloat("Shot Power", &m_ShotPower, m_MinShotPower, m_MaxShotPower);

		// 現在の GameState 表示
		const char* gsStr = "";
		switch (Game::GetInstance()->GetGameState())
		{
		case GameState::AimingDirection: gsStr = "AimingDirection"; break;
		case GameState::AimingPower:     gsStr = "AimingPower";     break;
		case GameState::ConfirmShot:     gsStr = "ConfirmShot";     break;
		case GameState::BallsMoving:     gsStr = "BallsMoving";     break;
		case GameState::TurnEnd:         gsStr = "TurnEnd";         break;
		case GameState::EnemyAttack:     gsStr = "EnemyAttack";     break;
		case GameState::ClearReward:     gsStr = "ClearReward";     break;
		case GameState::GameOver:        gsStr = "GameOver";        break;
		}
		ImGui::Text("GameState: %s", gsStr);

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

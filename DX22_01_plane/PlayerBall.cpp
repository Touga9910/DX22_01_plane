#include "PlayerBall.h"
//#include "Collision.h"
#include"Game.h"
#include"Ground.h"
#include"TableFrame.h"
#include"Camera.h"
#include "Pole.h"
#include "imgui/imgui.h"

#include<random>
#include<ctime>

using namespace std;
using namespace DirectX::SimpleMath;

//=======================================
//初期化処理
//=======================================
void PlayerBall::Init()
{
	m_MaxHP = 10;
	m_HP = m_MaxHP;

	// モデルの読み込み
	LoadModel("assets/model/GolfBall/golf_ball.obj", "assets/model/GolfBall");

	// 乱数生成エンジンとシードの初期化
	static std::random_device rd;
	static std::mt19937 gen(rd());

	// ランダムな範囲を定義 (例: -50.0 から 50.0 の範囲でランダムにする)
	const float MIN_RANGE = -200.0f;
	const float MAX_RANGE = 200.0f;

	// 浮動小数点数の一様分布を定義
	std::uniform_real_distribution<> distrib(MIN_RANGE, MAX_RANGE);


	m_Transform.position.x = 0.0f;
	m_Transform.position.z = 0.0f;
	m_Transform.position.y = 1.0f;

	SetInitialPosition(m_Transform.position);

	//スケールを調整
	m_Transform.scale = Vector3(2.0f, 2.0f, 2.0f);
	UpdateRadius();

	// ★ Groundから台の高さを取得して合わせる
	std::vector<Ground*> grounds = Game::GetInstance()->GetObjects<Ground>();
	if (grounds.size() > 0)
	{
		m_Transform.position.y = grounds[0]->GetFieldHeight();
	}
	else
	{
		m_Transform.position.y = 1.0f; // 万が一Groundが無い時の保険
	}

	//最初に速度を与える
	//m_Velocity.x = 1.0f;
	m_Velocity = Vector3::Zero;

	// 軌跡の初期位置を今のボールの位置にする
	m_LastTrailPos = m_Transform.position;
	m_TrajectoryPositions.clear();

	// ★ デフォルトモデルを設定
	m_TrajectoryModel = std::make_unique<BallTrajectoryModel>();

	// ★ 弾道予測用モデルの初期化（別途）
	InitTrajectoryVisualModel();
}

//=======================================
//更新処理
//=======================================
void PlayerBall::Update()
{
	if (IsDefeated())
	{
		return;
	}

	m_CurrentFrame++;

	// 状態の比較を enum class に変更 (0 ➔ State::Simulation)
	if (m_State == State::Simulation)
	{

		// --- キー入力による移動（デバッグ用などの加速への加算） ---
		float moveSpeed = 0.01f;
		Vector3 moveInput = Vector3::Zero;

		if (Input::GetKeyPress(VK_W)) moveInput.z += 4.0f; // 奥へ
		if (Input::GetKeyPress(VK_S)) moveInput.z -= 4.0f; // 手前へ
		if (Input::GetKeyPress(VK_A)) moveInput.x -= 4.0f; // 左へ
		if (Input::GetKeyPress(VK_D)) moveInput.x += 4.0f; // 右へ

		if (moveInput != Vector3::Zero)
		{
			moveInput.Normalize();
			m_Velocity += moveInput * moveSpeed;
		}

		// 速度が0に近づいたら停止判定
		if (m_Velocity.LengthSquared() < 0.03f)
		{
			m_StopCount++;
		}
		else
		{
			m_StopCount = 0;

			// 摩擦（減速）の計算
			float deceleratisonPower = m_Friction;

			Vector3 deceleration = -m_Velocity;	// 速度の逆ベクトルを計算
			deceleration.Normalize();			// ベクトルを正規化
			m_Acceleration = deceleration * deceleratisonPower;

			// 加速度を速度に加算
			m_Velocity += m_Acceleration;
		}

		// 10フレーム連続でほぼ動いていなければ静止状態へ
		if (m_StopCount > 10)
		{
			m_Velocity = Vector3(0.0f, 0.0f, 0.0f);
			// 状態の代入を enum class に変更 (1 ➔ State::Idle)
			m_State = State::Idle;
		}

		// 下に落ちたときはリスポーン
		if (m_Transform.position.y < -100)
		{
			m_Transform.position = Vector3(0.0f, 50.0f, 0.0f); // リスポーン座標
			m_Velocity = Vector3(0.0f, 0.0f, 0.0f);	// 速度リセット
			m_TrajectoryPositions.clear(); // 軌跡を消す
		}

		// Poleの位置を取得してカップイン判定
		vector<Pole*> pole = Game::GetInstance()->GetObjects<Pole>();
		if (pole.size() > 0)
		{
			Vector3 polePos = pole[0]->GetPosition();
			Collision::Sphere balCollision = { m_Transform.position, m_Radius };
			Collision::Sphere poleCollision = { polePos, 0.5f };

			if (Collision::CheckHit(balCollision, poleCollision))
			{
				// 状態の代入を enum class に変更 (2 ➔ State::Goal)
				m_State = State::Goal;
			}
		}

		// 軌跡を追加する処理
		// ★ 修正点: ここも整数ではなく列挙型でチェック
		if (m_State == State::Simulation)
		{
			float stepSize = 1.5f;

			Vector3 vecToCurrent = m_Transform.position - m_LastTrailPos;
			float dist = vecToCurrent.Length();

			if (dist >= stepSize)
			{
				vecToCurrent.Normalize();
				while (dist >= stepSize)
				{
					m_LastTrailPos += vecToCurrent * stepSize;
					m_TrajectoryPositions.push_back({ m_LastTrailPos, m_CurrentFrame, 1.0f });
					dist -= stepSize;
				}
			}
		}

		// カメラ方向による移動（※必要に応じてmoveInputの計算に組み込んでください）
		float dir = Camera::GetInstance().GetCameraDirection();
		Vector3 forward(sin(dir), 0, cos(dir));
		Vector3 right(cos(dir), 0, -sin(dir));
		float speed = 0.5f;
	}
	// ▼ if (m_State == State::Simulation) { ... } の直後に追加 ▼

	else if (m_State == State::Idle)
	{
		// TC-19: Simulation 中は到達しないため UpdateAim() は呼ばれない
		UpdateAim();  // 内部で GameState をチェックして処理を振り分ける
	}


	// --- 軌跡の削除・更新処理 ---
	int expirationFrame = m_CurrentFrame - TRAIL_DURATION_FRAMES;

	while (!m_TrajectoryPositions.empty() &&
		m_TrajectoryPositions.front().timestamp < expirationFrame)
	{
		m_TrajectoryPositions.erase(m_TrajectoryPositions.begin());
	}

	for (auto& point : m_TrajectoryPositions)
	{
		int remainingFrames = TRAIL_DURATION_FRAMES - (m_CurrentFrame - point.timestamp);
		point.lifeRatio = max(0.0f, (float)remainingFrames / TRAIL_DURATION_FRAMES);
	}

	// 物理演算を更新（BallBaseから受け継いだ座標更新処理など）
	UpdatePhysics();

	// カメラを追従させる
	//Camera::GetInstance().SetTarget(m_Transform.position);
}

//=======================================
//描画処理
//=======================================
void PlayerBall::Draw(Camera* cam)
{
	if (IsDefeated())
	{
		return;
	}
	//カメラを選択する
	cam->SetCamera();

	//カメラを追従させる
	//cam->SetTarget(m_Position);//カメラのターゲットを更新

	m_Shader.SetGPU();

	// インデックスバッファ・頂点バッファをセット
	m_MeshRenderer.BeforeDraw();

	for (const auto& trailPoint : m_TrajectoryPositions)
	{

		const auto& pos = trailPoint.position;//座標情報を取り出す

		float fade = trailPoint.lifeRatio;

		// フェードアウトに合わせてスケールを変化させる
		// 軌跡のスケールをfadeに比例させて、消える直前に小さくする
		float baseScale = 0.8f;
		float currentScare = baseScale * fade;

		// 最小スケールを設ける
		currentScare = max(0.01f, currentScare);

		// 軌跡は少し小さくする (0.5倍)
		Matrix r = Matrix::Identity; // 回転なし
		Matrix t = Matrix::CreateTranslation(pos);
		Matrix s = Matrix::CreateScale(m_Transform.scale.x * currentScare, m_Transform.scale.y * currentScare, m_Transform.scale.z * currentScare);

		Matrix worldmtx = s * r * t;
		DrawMesh(worldmtx);
	}

	// 1. 本来の向き（移動方向などを表す回転）
	Matrix rDirection = Matrix::CreateFromYawPitchRoll(m_Transform.rotation.y, m_Transform.rotation.x, m_Transform.rotation.z);

	// 2. 転がりの回転
	Matrix rRolling = Matrix::CreateFromQuaternion(m_RollingRotation);

	// 3. 行列の合成：転がり(rRolling)を適用した後に、本来の向き(rDirection)を合わせる
	Matrix r = rDirection * rRolling;
	Matrix t = Matrix::CreateTranslation(m_Transform.position.x, m_Transform.position.y, m_Transform.position.z);
	Matrix s = Matrix::CreateScale(m_Transform.scale.x, m_Transform.scale.y, m_Transform.scale.z);

	Matrix worldmtx = s * r * t;
	DrawMesh(worldmtx);
	// 矢印インジケーターを描画（旧 Arrow::Draw() 相当）
	DrawArrow(cam);
	// ★ 弾道予測線の描画（新しいメソッド）
	DrawTrajectoryLine();
}

//=======================================
//終了処理
//=======================================
void PlayerBall::Uninit()
{

}

// =======================================
// 倒された時の処理（HPが0になったとき）
// =======================================
void PlayerBall::Defeat()
{
	BallBase::Defeat();

	// プレイヤー専用：ゲームオーバーへ移行
	Game::GetInstance()->SetGameState(GameState::GameOver);
}

//=======================================
//ポケットに入ったときの処理
//=======================================
void PlayerBall::OnPocketHit()
{
	// HPを減らす処理
	TakeDamage(1);

	// 初期位置に戻す
	ResetToInitialPosition();

	m_TrajectoryPositions.clear();
	m_PrePositions.clear();
	m_LastTrailPos = m_Transform.position;
	m_StopCount = 0;
	m_RollingRotation = DirectX::SimpleMath::Quaternion::Identity;
	m_State = State::Idle;
}

void PlayerBall::TakeDamage(int damage)
{
	Damage(damage);
}

//=======================================
// エイム操作（旧 Arrow::Update() + StageBase のステートマシンを統合）
// 呼び出し条件: PlayerBall::State::Idle の時のみ
//=======================================
void PlayerBall::UpdateAim()
{
	GameState gs = Game::GetInstance()->GetGameState();

	if (gs == GameState::AimingDirection)
	{
		if (Input::GetKeyPress(VK_LEFT))  m_AimAngle -= 0.02f;
		if (Input::GetKeyPress(VK_RIGHT)) m_AimAngle += 0.02f;

		if (Input::GetKeyTrigger(VK_SPACE))
			Game::GetInstance()->SetGameState(GameState::AimingPower);
	}
	else if (gs == GameState::AimingPower)
	{
		if (Input::GetKeyPress(VK_UP))
			m_ShotPower = min(m_MaxShotPower, m_ShotPower + m_PowerStep);

		if (Input::GetKeyPress(VK_DOWN))
			m_ShotPower = max(m_MinShotPower, m_ShotPower - m_PowerStep);

		if (Input::GetKeyTrigger(VK_SPACE))
			Game::GetInstance()->SetGameState(GameState::ConfirmShot);
	}
	else if (gs == GameState::ConfirmShot)
	{
		if (Input::GetKeyTrigger(VK_SPACE))
		{
			Shot(GetShotVector());
			m_State = State::Simulation;
			m_PrePositions.clear();
			m_PreTrajectoryDirty = true;
			m_StopCount = 0;
			Game::GetInstance()->SetGameState(GameState::BallsMoving);
		}
	}

	// 方向を変えたときにフラグを変更する
	bool previewChanged =
		fabs(m_AimAngle - m_LastPreviewAimAngle) > 0.001f ||
		fabs(m_ShotPower - m_LastPreviewShotPower) > 0.001f ||
		(m_Transform.position - m_LastPreviewPosition).LengthSquared() > 0.01f;

	// 弾道予測の更新が必要な場合に再計算する
	if (m_PreTrajectoryDirty || previewChanged || m_PrePositions.empty())
	{
		GeneratePreTrajectory(GetShotVector());

		m_LastPreviewAimAngle = m_AimAngle;
		m_LastPreviewShotPower = m_ShotPower;
		m_LastPreviewPosition = m_Transform.position;
		m_PreTrajectoryDirty = false;
	}
}

//=======================================
// ショットベクトルを計算して返す（旧 Arrow::GetVector() 相当）
//=======================================
Vector3 PlayerBall::GetShotVector() const
{
	// TC-18: m_AimAngle=0, m_ShotPower=5 → Vector3(sin(0),0,cos(0))×5 = Vector3(0,0,5)
	return Vector3(sin(m_AimAngle), 0.0f, cos(m_AimAngle)) * m_ShotPower;
}

//=======================================
// 矢印インジケーター描画（旧 Arrow::Draw() 相当）
// AimingDirection: 固定長の方向矢印
// AimingPower / ConfirmShot: パワー比例の矢印
//=======================================
void PlayerBall::DrawArrow(Camera* cam)
{
	/*
	GameState gs = Game::GetInstance()->GetGameState();
	if (m_State != State::Idle) return;
	if (gs == GameState::BallsMoving || gs == GameState::TurnEnd) return;

	Vector3 shotDir(sin(m_AimAngle), 0.0f, cos(m_AimAngle));

	// AimingDirection は固定長、それ以外はパワー比例
	float arrowLength = (gs == GameState::AimingDirection)
		? m_MaxShotPower * 1.5f
		: m_ShotPower * 2.0f;

	Vector3 midPos = m_Transform.position + shotDir * (arrowLength * 0.5f);
	const float thickness = 0.3f;

	Matrix s = Matrix::CreateScale(thickness, thickness, arrowLength);
	Vector3 up = Vector3::Up;
	if (abs(shotDir.y) > 0.99f) up = Vector3::UnitZ;
	Matrix rt = Matrix::CreateWorld(midPos, shotDir, up);

	DrawMesh(s * rt);
	*/
}
//=======================================
// 弾道予測を生成する関数
//=======================================
void PlayerBall::GeneratePreTrajectory(const DirectX::SimpleMath::Vector3& initialVelocity)
{
	m_PrePositions.clear();
	m_PreviewHitBall = false;
	m_PreviewGhostBallPosition = Vector3::Zero;
	m_PreviewHitBallPosition = Vector3::Zero;
	m_PreviewObjectBallDirection = Vector3::Zero;

	const int PREDICTION_FRAMES = 240;
	const int MAX_PREVIEW_POINTS = 120;
	const float PREVIEW_POINT_INTERVAL = 1.0f;
	const float PREVIEW_BALL_HIT_SCALE = 1.0f;

	m_PrePositions.reserve(MAX_PREVIEW_POINTS);

	if (!m_TrajectoryModel)
	{
		m_TrajectoryModel = std::make_unique<BallTrajectoryModel>();
	}

	Vector3 simPosition = m_Transform.position;
	Vector3 simVelocity = initialVelocity;
	Vector3 simAcceleration;


	std::vector<Collision::Segment> walls;
	std::vector<TableFrame*> frames = Game::GetInstance()->GetObjects<TableFrame>();

	float fieldHeight = m_Transform.position.y;

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

	m_PrePositions.push_back({ simPosition, 0, 1.0f });

	std::vector<BallBase*> balls = Game::GetInstance()->GetObjects<BallBase>();

	for (int frame = 0; frame < PREDICTION_FRAMES; ++frame)
	{
		Vector3 oldPosition = simPosition;

		simVelocity.y = 0.0f;
		m_TrajectoryModel->SimulateStep(simPosition, simVelocity, simAcceleration);
		simPosition.y = fieldHeight;
		simVelocity.y = 0.0f;

		Vector3 frameMove = simPosition - oldPosition;
		frameMove.y = 0.0f;

		float maxStep = m_Radius * 0.5f;
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

				if (distance <= m_Radius)
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

					simPosition = contactPoint + normal * m_Radius;
					simPosition.y = fieldHeight;
					m_PrePositions.push_back({ visualContactPoint, 0, 1.0f });
					hit = true;
					break;
				}
			}

			if (hit) break;

			for (BallBase* other : balls)
			{
				if (other == this) continue;
				if (other->IsDefeated()) continue;

				Collision::Sphere otherSphere = other->GetSphere();
				Vector3 diff = simPosition - otherSphere.center;
				diff.y = 0.0f;

				float myHitRadius = m_Radius * PREVIEW_BALL_HIT_SCALE;
				float otherHitRadius = otherSphere.radius * PREVIEW_BALL_HIT_SCALE;
				float minDist = myHitRadius + otherHitRadius;
				float minDistSq = minDist * minDist;

				if (diff.LengthSquared() <= minDistSq)
				{
					Vector3 normal = diff;

					if (normal.LengthSquared() > 0.0001f)
					{
						normal.Normalize();
					}
					else
					{
						normal = -stepMove;
						normal.y = 0.0f;

						if (normal.LengthSquared() > 0.0001f)
							normal.Normalize();
						else
							normal = Vector3::UnitZ;
					}

					float ghostDistance = m_Radius + otherSphere.radius;
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

		if (hit || m_TrajectoryModel->ShouldStop(simVelocity))
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
// ★ 新規メソッド: 弾道予測用の描画モデルを初期化
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

	Vector3 startPos = start;
	Vector3 endPos = end;
	startPos.y += yOffset;
	endPos.y += yOffset;

	Vector3 forward = endPos - startPos;
	float distance = forward.Length();
	if (distance <= 0.0001f)
	{
		return;
	}

	forward /= distance;

	Vector3 midPos = (startPos + endPos) * 0.5f;
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
	const int segmentCount = 48;

	for (int i = 0; i < segmentCount; ++i)
	{
		float angle0 = DirectX::XM_2PI * static_cast<float>(i) / static_cast<float>(segmentCount);
		float angle1 = DirectX::XM_2PI * static_cast<float>(i + 1) / static_cast<float>(segmentCount);

		Vector3 p0 = center + Vector3(cos(angle0) * radius, 0.0f, sin(angle0) * radius);
		Vector3 p1 = center + Vector3(cos(angle1) * radius, 0.0f, sin(angle1) * radius);
		DrawGuideSegment(p0, p1, thickness, yOffset, materialIndex);
	}
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
			float startOffset = m_Radius + MAIN_LINE_THICKNESS;

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
			m_Radius,
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

void PlayerBall::DrawImGui()
{
	// 親クラスの共通UIを呼ぶ
	BallBase::DrawImGui("PlayerBall");

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
		case GameState::EnemyAttack:	 gsStr = "EnemyAttack";		break;
		case GameState::ClearReward:	 gsStr = "ClearReward";		break;
		case GameState::GameOver:		 gsStr = "GameOver";			break;
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

#include "PlayerBall.h"
//#include "Collision.h"
#include"Game.h"
#include"Ground.h"
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
}

//=======================================
//更新処理
//=======================================
void PlayerBall::Update()
{
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
	//カメラを選択する
	cam->SetCamera();

	//カメラを追従させる
	//cam->SetTarget(m_Position);//カメラのターゲットを更新

	m_Shader.SetGPU();

	// インデックスバッファ・頂点バッファをセット
	m_MeshRenderer.BeforeDraw();

	if (m_State != State::Simulation && m_PrePositions.size() >= 2) // 2点以上ないと線が引けない
	{

		const float Y_OFFSET = 0.0f;

		// リストの「最後の一つ手前」までループする
		for (size_t i = 0; i < m_PrePositions.size() - 1; i++)
		{
			// スタート地点とゴール地点を取得
			Vector3 startPos = m_PrePositions[i].position;
			Vector3 endPos = m_PrePositions[i + 1].position;

			// 2点間の距離を計算（これをZ軸のスケールにする）
			float distance = (endPos - startPos).Length();

			// 距離が0なら描画しない
			if (distance <= 0.0001f) continue;

			// 2点の中間地点を計算（ここにモデルを置く）
			Vector3 midPos = (startPos + endPos) * 0.5f;
			
			// Y軸方向にずらす
			midPos.y += Y_OFFSET;

			// 線の太さ
			float thickness = 0.2f;

			// ■ SRT行列の作成 ■

			// 1. スケール：X,Yは太さ、Zは長さ（距離）
			// ※球体モデルは直径1.0と仮定。もし直径が大きいモデルなら調整が必要
			Matrix s = Matrix::CreateScale(thickness, thickness, distance);

			// 2. 回転と位置：CreateWorldを使うと「ある位置(midPos)で、ある方向(forward)を向く行列」が生成可能
			Vector3 forward = endPos - startPos; // 向きたい方向
			forward.Normalize();

			// 上方向ベクトル（真上か、forwardが真上のときはZ軸などを仮定）
			Vector3 up = Vector3::Up;
			if (abs(forward.y) > 0.99f) up = Vector3::UnitZ;

			// 回転と平行移動を合わせた行列を作成
			// CreateWorld(位置, 前方ベクトル, 上方ベクトル)
			Matrix rt = Matrix::CreateWorld(midPos, forward, up);

			// 全体を合成
			Matrix worldmtx = s * rt;
			DrawMesh(worldmtx);
		}
	}


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
}

//=======================================
//終了処理
//=======================================
void PlayerBall::Uninit()
{

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
		// TC-10: 左キーで方向角度を減少
		if (Input::GetKeyPress(VK_LEFT))  m_AimAngle -= 0.02f;
		// TC-11: 右キーで方向角度を増加
		if (Input::GetKeyPress(VK_RIGHT)) m_AimAngle += 0.02f;
		// TC-15: SPACE（Trigger）でパワー選択へ（TC-12: 方向ロック開始）
		if (Input::GetKeyTrigger(VK_SPACE))
			Game::GetInstance()->SetGameState(GameState::AimingPower);
	}
	else if (gs == GameState::AimingPower)
	{
		// TC-13: 上キーでパワー増加（上限クランプ）
		if (Input::GetKeyPress(VK_UP))
			m_ShotPower = min(m_MaxShotPower, m_ShotPower + m_PowerStep);
		// TC-14: 下キーでパワー減少（下限クランプ）
		if (Input::GetKeyPress(VK_DOWN))
			m_ShotPower = max(m_MinShotPower, m_ShotPower - m_PowerStep);
		// TC-16: SPACE（Trigger）でショット確認へ
		if (Input::GetKeyTrigger(VK_SPACE))
			Game::GetInstance()->SetGameState(GameState::ConfirmShot);
	}
	else if (gs == GameState::ConfirmShot)
	{
		// TC-23: 毎フレーム弾道予測を更新（既存 Draw() が m_PrePositions を描画）
		GeneratePreTrajectory(GetShotVector());

		// TC-17: SPACE（Trigger）でショット実行
		if (Input::GetKeyTrigger(VK_SPACE))
		{
			Shot(GetShotVector());                   // TC-17: Velocity をセット
			m_State = State::Simulation;             // TC-17: State を Simulation へ
			m_PrePositions.clear();                  // 予測弾道をクリア
			m_StopCount = 0;                         // 停止カウントをリセット
			Game::GetInstance()->SetGameState(GameState::BallsMoving); // TC-17
		}
	}
	// BallsMoving / TurnEnd は上記のいずれにも該当しないため何もしない
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
}
//=======================================
// 弾道予測を生成する関数
//=======================================
void PlayerBall::GeneratePreTrajectory(const DirectX::SimpleMath::Vector3& initialVelocity)
{
	// 既存のリストをクリア
	m_PrePositions.clear();

	// 予測の継続時間 (フレーム数)
	const int PREDICTION_FRAMES = 120;

	// 現在の状態をコピー（シミュレーション用）
	Vector3 simPosition = m_Transform.position;
	Vector3 simVelocity = initialVelocity; // 予測したい初速
	Vector3 simAcceleration;

	// 予測シミュレーション用の物理定数
	const float gravity = 0.1f;
	const float deceleratisonPower = m_Friction;

	//始めの点を追加
	m_PrePositions.push_back({ simPosition, 0, 1.0f });

	// 予測シミュレーションの実行
	for (int frame = 0; frame < PREDICTION_FRAMES; ++frame)
	{
		//Vector3 oldSimPosition = simPosition;

		// 1. 減速の計算 (m_State==0 ブロックから流用)
		if (simVelocity.LengthSquared() > 0.03f)
		{	
			Vector3 deceleration = -simVelocity;
			deceleration.Normalize();
			simAcceleration = deceleration * deceleratisonPower;
			simVelocity += simAcceleration;
		}

		// 2. 重力
		//simVelocity.y -= gravity;
		simVelocity.y = 0.0f;

		// 3. 座標の更新
		simPosition += simVelocity;
		simVelocity.y = 1.0f;


		// 距離が近すぎる場合は追加しない（無駄な描画を防ぐため）
		float distSq = (simPosition - m_PrePositions.back().position).LengthSquared();

		// 点を追加（数値を小さくすると滑らかになります）
		if (distSq > 3.0f * 3.0f)
		{
			m_PrePositions.push_back({ simPosition, 0, 1.0f });
		}
	}
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
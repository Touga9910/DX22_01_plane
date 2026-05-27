#include "GolfBall.h"
#include "Collision.h"
#include"Game.h"
#include"Ground.h"
#include"Camera.h"
#include "Pole.h"

#include<random>
#include<ctime>

using namespace std;
using namespace DirectX::SimpleMath;

//=======================================
//初期化処理
//=======================================
void GolfBall::Init()
{
	// メッシュ読み込み
	StaticMesh staticmesh;

	//3Dモデルデータ
	std::u8string modelFile = u8"assets/model/golfball/golf_ball.obj";

	//テクスチャディレクトリ
	std::string texDirectory = "assets/model/golfball";

	//Meshを読み込む
	std::string tmpStr1(reinterpret_cast<const char*>(modelFile.c_str()), modelFile.size());
	staticmesh.Load(tmpStr1, texDirectory);

	m_MeshRenderer.Init(staticmesh);

	// シェーダオブジェクト生成
	m_Shader.Create("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");

	// サブセット情報取得
	m_subsets = staticmesh.GetSubsets();

	// テクスチャ情報取得
	m_Textures = staticmesh.GetTextures();

	// マテリアル情報取得	
	std::vector<MATERIAL> materials = staticmesh.GetMaterials();

	// マテリアル数分ループ
	for (int i = 0; i < materials.size(); i++)
	{
		// マテリアルオブジェクト生成
		std::unique_ptr<Material> m = std::make_unique<Material>();

		// マテリアル情報をセット
		m->Create(materials[i]);

		// マテリアルオブジェクトを配列に追加
		m_Materials.push_back(std::move(m));
	}

	// 乱数生成エンジンとシードの初期化
	static std::random_device rd;
	static std::mt19937 gen(rd());

	// ランダムな範囲を定義 (例: -50.0 から 50.0 の範囲でランダムにする)
	const float MIN_RANGE = -200.0f;
	const float MAX_RANGE = 200.0f;

	// 浮動小数点数の一様分布を定義
	std::uniform_real_distribution<> distrib(MIN_RANGE, MAX_RANGE);


	m_Position.x = distrib(gen);
	m_Position.z = distrib(gen);
	m_Position.y = -99.0f;

	//モデルによってスケールを調整
	m_Scale.x = 1;
	m_Scale.y = 1;
	m_Scale.z = 1;

	//最初に速度を与える
	m_Velocity.x = 1.0f;

	// 軌跡の初期位置を今のボールの位置にする
	m_LastTrailPos = m_Position;
	m_TrajectoryPositions.clear();
}

//=======================================
//更新処理
//=======================================
void GolfBall::Update()
{

	m_CurrentFrame++;

	if (m_State == 0 )
	{
		Vector3 oldPos = m_Position;//1フレーム前の位置を記録

		// 以下追加
		// --- 1. キー入力による移動（速度への加算） ---
		float moveSpeed = 0.01f; // 加速の強さ（好みに合わせて調整）
		Vector3 moveInput = Vector3::Zero;

		if (Input::GetKeyPress(VK_W)) moveInput.z += 1.0f; // 奥へ
		if (Input::GetKeyPress(VK_S)) moveInput.z -= 1.0f; // 手前へ
		if (Input::GetKeyPress(VK_A)) moveInput.x -= 1.0f; // 左へ
		if (Input::GetKeyPress(VK_D)) moveInput.x += 1.0f; // 右へ

		if (moveInput != Vector3::Zero)
		{
			moveInput.Normalize();
			m_Velocity += moveInput * moveSpeed;
			m_State = 0; // キー入力があったら「移動状態」にする
		}

		//速度が0に近づいたら停止
		if (m_Velocity.LengthSquared() < 0.03f)
		{
			//m_Velocity = Vector3(0.0f, 0.0f, 0.0f);
			m_StopCount++;
		}
		else
		{
			m_StopCount = 0;

			//現速度(1フレーム当たりどれくらい減速するか)
			float deceleratisonPower = 0.02f;

			Vector3 deceleration = -m_Velocity;	//速度の逆ベクトルを計算
			deceleration.Normalize();			// ベクトルを正規化
			m_Acceleration = deceleration * deceleratisonPower;

			//加速度を速度に加算
			m_Velocity += m_Acceleration;

		}
		// 10フレーム連続でほぼ動いていなければ静止状態へ
		if (m_StopCount > 10)
		{
			m_Velocity = Vector3(0.0f, 0.0f, 0.0f);
			m_State = 1; //静止状態
		}

		/*
		//重力
		const float gravity = 0.1f;
		m_Velocity.y -= gravity;

		//速度を座標に加算
		m_Position += m_Velocity;
		*/

		// Y方向（上下）には絶対に動かないようにする
		m_Velocity.y = 0.0f;

		//速度を座標に加算
		m_Position += m_Velocity;

		// 念のため、何が起きても高さが一定になるように固定
		m_Position.y = 1.0f;

		//ボールモデルの直径
		float radius = 1.0f;

		/*
		//Groundの頂点データの取得
		vector<Ground*>grounds = Game::GetInstance()->GetObjects<Ground>();
		vector<VERTEX_3D> vertices;
		for (auto& g : grounds)	//Groundの数だけループ
		{
			vector<VERTEX_3D>vecs = g->GetVertices();	//頂点の数だけループ
			for (auto& v : vecs)
			{
				vertices.emplace_back(v);
			}
		}

		float moveDistance = 9999;//移動距離
		Vector3 contactPoint;	//接触点
		Vector3 normal;

		//地面との当たり判定
		for (int i = 0; i < vertices.size(); i += 3)
		{
			//三角形ポリゴン
			Collision::Polygon collisionPolygon =
			{
				vertices[i + 0].position,
				vertices[i + 1].position,
				vertices[i + 2].position
			};
			Vector3 cp;//接触点
			Collision::Segment collisionSegment = { oldPos,m_Position };
			Collision::Sphere collisionSphere = { m_Position,radius };

			//線分とポリゴンの当たり判定
			if (Collision::CheckHit(collisionSegment, collisionPolygon, cp))
			{
				float md = 0;
				Vector3 np = Collision::moveSphere(collisionSegment, radius, collisionPolygon, cp, md);
				if (moveDistance > md)
				{
					moveDistance = md;
					m_Position = np;
					contactPoint = cp;
					normal = Collision::GetNormal(collisionPolygon);
				}
			}
			//球体とポリゴンの当たり判定
			else if (Collision::CheckHit(collisionSphere, collisionPolygon, cp))
			{
				Vector3 np = Collision::moveSphere(collisionSphere, collisionPolygon, cp);
				float md = (np - oldPos).Length();
				if (moveDistance > md)
				{
					moveDistance = md;
					m_Position = np;
					contactPoint = cp;
					normal = Collision::GetNormal(collisionPolygon);
				}
			}
		}

		if (moveDistance != 9999)//もし当たっていたら
		{
			//MessageBoxA(NULL, "当たった", "確認", MB_OK);

			//ボールの速度ベクトルの法線方向成分と接戦方向成分を分解
			float velocityNorma = Collision::Dot(m_Velocity, normal);
			Vector3 v1 = velocityNorma * normal;//法線方向成分
			Vector3 v2 = m_Velocity - v1;//接戦方向成分

			//反射ベクトルを計算
			const float restisusion = 0.5f;
			const float friction = 0.95f;
			Vector3 reflecterVelocity = v2 * friction - v1 * restisusion;

			//ボール速度の更新
			m_Velocity = reflecterVelocity;


		}
		*/
		// 下に落ちたときはリスポーン
		if (m_Position.y < -100)
		{
			m_Position = Vector3(0.0f, 50.0f, 0.0f); //リスポーン座標
			m_Velocity = Vector3(0.0f, 0.0f, 0.0f);	//速度リセット

			// リスポーン時に軌跡を消す
			m_TrajectoryPositions.clear();
		}

		//Poleの位置を取得
		vector<Pole*> pole = Game::GetInstance()->GetObjects<Pole>();
		if (pole.size() > 0)
		{
			Vector3 polePos = pole[0]->GetPosition();

			Collision::Sphere balCollision = { m_Position,radius };//ゴルフボール当たり判定

			Collision::Sphere poleCollision = { polePos,0.5f };//ポール当たり判定

			if (Collision::CheckHit(balCollision, poleCollision))
			{
				m_State = 2;//カップイン

			}

		}

		//軌跡を追加する処理
		if (m_State == 0)
		{
			// 点を打つ間隔
			float stepSize = 1.5f;

			// 最後に打った場所から、現在の場所までのベクトルと距離
			Vector3 vecToCurrent = m_Position - m_LastTrailPos;
			float dist = vecToCurrent.Length();

			// 一定以上離れていたら、間を埋めるように点を追加
			if (dist >= stepSize)
			{
				// 方向ベクトルを正規化（長さ1にする）
				vecToCurrent.Normalize();

				// 距離分だけループして点を打つ
				// while文を使うことで、1フレームに大きく動いても間を全部埋めれる
				while (dist >= stepSize)
				{
					// 次の点の座標を計算
					m_LastTrailPos += vecToCurrent * stepSize;

					// リストに追加
					m_TrajectoryPositions.push_back({ m_LastTrailPos,m_CurrentFrame, 1.0f });

					// 残りの距離を減らす
					dist -= stepSize;
				}
			}
		}

		//カメラ方向によって移動方向を変える
		float dir = Camera::GetInstance().GetCameraDirection();

		// 前方（カメラが向く方向）
		Vector3 forward(sin(dir), 0, cos(dir));

		// 右方向（前方を90度回転）
		Vector3 right(cos(dir), 0, -sin(dir));

		// 移動速度
		float speed = 0.5f;

		/*
		if (Input::GetKeyPress(VK_W)) m_Position -= forward * speed;
		if (Input::GetKeyPress(VK_S)) m_Position += forward * speed;
		if (Input::GetKeyPress(VK_A)) m_Position += right * speed;
		if (Input::GetKeyPress(VK_D)) m_Position -= right * speed;
		*/

		//移動方向によってボールを回転させる
		if (oldPos != m_Position)
		{
			float rotateX = m_Position.x - oldPos.x;
			float rotateY = m_Position.y - oldPos.y;
			float rotateZ = m_Position.z - oldPos.z;
			m_Rotation += {rotateZ, 0, rotateX};
		}
	}

	// 軌跡を削除する処理（いつでも作動するように、m_State == 0から外しておく）
	
	// 削除のしきい値となるフレーム番号を計算 (現在のフレーム - 寿命)
	int expirationFrame = m_CurrentFrame - TRAIL_DURATION_FRAMES;

	// リストの先頭から、寿命が尽きた要素を削除（リストの要素は古い順に入っている）
	while (!m_TrajectoryPositions.empty() &&
		m_TrajectoryPositions.front().timestamp < expirationFrame)
	{
		// リストの先頭（最も古い要素）を削除
		m_TrajectoryPositions.erase(m_TrajectoryPositions.begin());
	}

	// 軌跡の点の lifeRatio を更新
	for (auto& point : m_TrajectoryPositions)
	{
		// 寿命の残りフレーム数を計算
		int remainingFrames = TRAIL_DURATION_FRAMES - (m_CurrentFrame - point.timestamp);

		// 残りフレーム数から lifeRatio を計算 (0.0 ～ 1.0)
		point.lifeRatio = max(0.0f, (float)remainingFrames / TRAIL_DURATION_FRAMES);
	}

	//カメラを追従させる
	Camera::GetInstance().SetTarget(m_Position);
}

//=======================================
//描画処理
//=======================================
void GolfBall::Draw(Camera* cam)
{
	//カメラを選択する
	cam->SetCamera();

	//カメラを追従させる
	//cam->SetTarget(m_Position);//カメラのターゲットを更新

	m_Shader.SetGPU();

	// インデックスバッファ・頂点バッファをセット
	m_MeshRenderer.BeforeDraw();

	if (m_State != 0 && m_PrePositions.size() >= 2) // 2点以上ないと線が引けない
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

			// 2. 回転と位置：CreateWorldを使うと「ある位置(midPos)で、ある方向(forward)を向く行列」が一発で作れます
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
			Renderer::SetWorldMatrix(&worldmtx);

			// 描画実行
			for (int j = 0; j < m_subsets.size(); j++)
			{
				m_Materials[m_subsets[j].MaterialIdx]->SetGPU();
				if (m_Materials[m_subsets[j].MaterialIdx]->isTextureEnable())
				{
					m_Textures[m_subsets[j].MaterialIdx]->SetGPU();
				}
				m_MeshRenderer.DrawSubset(
					m_subsets[j].IndexNum,
					m_subsets[j].IndexBase,
					m_subsets[j].VertexBase);
			}
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
		Matrix s = Matrix::CreateScale(m_Scale.x * currentScare, m_Scale.y * currentScare, m_Scale.z * currentScare);

		Matrix worldmtx = s * r * t;
		Renderer::SetWorldMatrix(&worldmtx); // GPUにセット

		// サブセット描画（現在のボールと同じマテリアルを使う）
		for (int i = 0; i < m_subsets.size(); i++)
		{
			m_Materials[m_subsets[i].MaterialIdx]->SetGPU();
			if (m_Materials[m_subsets[i].MaterialIdx]->isTextureEnable())
			{
				m_Textures[m_subsets[i].MaterialIdx]->SetGPU();
			}
			m_MeshRenderer.DrawSubset(
				m_subsets[i].IndexNum,
				m_subsets[i].IndexBase,
				m_subsets[i].VertexBase);
		}
	}

	// SRT情報作成
	Matrix r = Matrix::CreateFromYawPitchRoll(m_Rotation.y, m_Rotation.x, m_Rotation.z);
	Matrix t = Matrix::CreateTranslation(m_Position.x, m_Position.y, m_Position.z);
	Matrix s = Matrix::CreateScale(m_Scale.x, m_Scale.y, m_Scale.z);

	Matrix worldmtx;
	worldmtx = s * r * t;
	Renderer::SetWorldMatrix(&worldmtx); // GPUにセット

	//マテリアル数分ループ 
	for (int i = 0; i < m_subsets.size(); i++)
	{
		// マテリアルをセット(サブセット情報の中にあるマテリアルインデックスを使用)
		m_Materials[m_subsets[i].MaterialIdx]->SetGPU();

		if (m_Materials[m_subsets[i].MaterialIdx]->isTextureEnable())
		{
			m_Textures[m_subsets[i].MaterialIdx]->SetGPU();
		}

		m_MeshRenderer.DrawSubset(
			m_subsets[i].IndexNum,		// 描画するインデックス数
			m_subsets[i].IndexBase,		// 最初のインデックスバッファの位置	
			m_subsets[i].VertexBase);	// 頂点バッファの最初から使用
	}
}

//=======================================
//終了処理
//=======================================
void GolfBall::Uninit()
{

}

// GolfBall.cpp

//=======================================
// 弾道予測を生成する関数
//=======================================
void GolfBall::GeneratePreTrajectory(const DirectX::SimpleMath::Vector3& initialVelocity)
{
	// 既存のリストをクリア
	m_PrePositions.clear();

	// 予測の継続時間 (フレーム数)
	const int PREDICTION_FRAMES = 120;

	// 現在の状態をコピー（シミュレーション用）
	Vector3 simPosition = m_Position;
	Vector3 simVelocity = initialVelocity; // 予測したい初速
	Vector3 simAcceleration;

	// 予測シミュレーション用の物理定数
	const float gravity = 0.1f;
	const float deceleratisonPower = 0.02f;

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
#include "BallBase.h"

#include "Collision.h"
#include "Game.h"
#include "TableFrame.h"
#include "Pocket.h"
#include "imgui/imgui.h"
#include "EnemyBall.h"

#include <algorithm>
#include <cmath>

using namespace std;
using namespace DirectX::SimpleMath;

void BallBase::Damage(int damage)
{
	if (m_IsDefeated)
	{
		return;
	}

	// 防御力を考慮したダメージ計算
	int finalDamage = damage - m_Status.defense;    // 防御力を引いた最終ダメージ

	if (finalDamage < 1)
	{
		finalDamage = 1;
	}

	m_HP -= finalDamage;

	// HPが0以下になった場合は倒された状態にする
	if (IsHpZero())
	{
		Defeat();
	}
}

void BallBase::TakeDamage(int damage)
{
	// 外部から受け取ったダメージを、共通のダメージ処理へ渡す
	Damage(damage);
}

void BallBase::Defeat()
{
	if (m_IsDefeated)
	{
		return;
	}

	m_IsDefeated = true;

	// 倒されたら速度を止める
	m_Velocity = DirectX::SimpleMath::Vector3::Zero;
	m_Acceleration = DirectX::SimpleMath::Vector3::Zero;
}

void BallBase::UpdatePhysics()
{
	m_OldPosition = m_Transform.position;

	// 壁の当たり判定についての処理（動的サブステップ方式）//

	// Y方向（上下）には絶対に動かないようにする
	m_Velocity.y = 0.0f;

	std::vector<Collision::Segment> walls;                          // テーブル枠から集めた壁情報
	std::vector<TableFrame*> frames = Game::GetInstance()->GetObjects<TableFrame>();

	for (TableFrame* frame : frames)
	{
		std::vector<Collision::Segment> frameWalls = frame->GetWalls();

		walls.insert(
			walls.end(),
			frameWalls.begin(),
			frameWalls.end());
	}

	if (!walls.empty())
	{
		// 1フレームの移動距離を計算
		float moveDistance = m_Velocity.Length();

		// 1ステップで進んでいい最大の距離（すり抜けないよう半径の半分以下にする）
		float maxStep = m_Radius * 0.5f;

		// 必要な分割数（速度が遅ければ1回、速ければ自動で増える）
		int subSteps = (std::max)(1, static_cast<int>(std::ceil(moveDistance / maxStep)));

		std::vector<BallBase*> balls = Game::GetInstance()->GetObjects<BallBase>();
		for (BallBase* other : balls)
		{
			if (other == this) continue;
			if (other->IsDefeated()) continue;

			Vector3 relativeVelocity = m_Velocity - other->m_Velocity;
			float relativeSpeed = relativeVelocity.Length();

			int relativeSubSteps = (std::max)(1, static_cast<int>(std::ceil(relativeSpeed / maxStep)));
			subSteps = (std::max)(subSteps, relativeSubSteps);
		}

		// 1ステップあたりの移動量
		Vector3 stepVelocity = m_Velocity / static_cast<float>(subSteps);

		for (int step = 0; step < subSteps; step++)
		{
			// 少しだけ移動させる
			m_Transform.position += stepVelocity;

			// その位置で壁との当たり判定
			for (int i = 0; i < static_cast<int>(walls.size()); i++)
			{
				Vector3 contactPoint;
				float distance = Collision::DistancePointToSegment(m_Transform.position, walls[i], contactPoint);

				// 距離が半径以下なら衝突
				if (distance <= m_Radius)
				{
					// 法線の計算
					Vector3 normal = m_Transform.position - contactPoint;
					normal.y = 0.0f;

					if (normal.LengthSquared() > 0.0001f)
					{
						normal.Normalize();
					}
					else
					{
						// 万が一完全に重なった場合の安全装置
						Vector3 wallVec = walls[i].end - walls[i].start;
						wallVec.Normalize();
						normal = Vector3(-wallVec.z, 0.0f, wallVec.x);
					}

					// 進行方向と法線が逆向きになるよう調整
					if (Collision::Dot(m_Velocity, normal) > 0)
					{
						normal = -normal;
					}

					// 1. めり込み防止（衝突点から半径分押し返す）
					m_Transform.position = contactPoint + normal * m_Radius;

					// 2. 反射処理
					float dot = Collision::Dot(m_Velocity, normal);
					if (dot < 0)
					{
						float restitution = m_Restitution; // 反発係数
						m_Velocity = m_Velocity - normal * (2.0f * dot) * restitution;

						// 反射したので、残りのステップの移動方向も「反射後の速度」に更新する
						stepVelocity = m_Velocity / static_cast<float>(subSteps);
					}
				}
			}

			// ボール同士の衝突判定（二重処理防止版）
			std::vector<BallBase*> balls = Game::GetInstance()->GetObjects<BallBase>();
			bool foundSelf = false;  // 自分を見つけたかのフラグ

			for (BallBase* other : balls)
			{
				if (other->IsDefeated()) continue;

				// 自分を見つけるまでスキップ
				if (!foundSelf)
				{
					if (other == this) foundSelf = true;
					continue;  // 自分自身も含めてスキップ
				}

				// ↓ ここから「自分より後ろのボール」とだけ判定される
				Vector3 diff = m_Transform.position - other->m_Transform.position;
				float distance = diff.Length();
				float minDist = m_Radius + other->m_Radius;

				if (distance < minDist && distance > 0.0001f)
				{
					Vector3 normal = diff;
					normal.Normalize();

					float overlap = minDist - distance;
					m_Transform.position += normal * (overlap * 0.5f);
					other->m_Transform.position -= normal * (overlap * 0.5f);

					float myDot = Collision::Dot(m_Velocity, normal);
					float otherDot = Collision::Dot(other->m_Velocity, normal);

					if (myDot - otherDot < 0)
					{
						float restitution = m_Restitution;

						// 質量を考慮した速度変化量
						float totalMass = m_Mass + other->m_Mass;
						float myRatio = (2.0f * other->m_Mass) / totalMass;
						float otherRatio = (2.0f * m_Mass) / totalMass;

						m_Velocity -= normal * myRatio * (myDot - otherDot) * restitution;
						other->m_Velocity += normal * otherRatio * (myDot - otherDot) * restitution;

						stepVelocity = m_Velocity / static_cast<float>(subSteps);

						// ==========================================================
						// 衝突時のダメージ適用処理
						// ==========================================================
						// 仮のダメージ量
						int damageAmount = 1;

						// ① 自分が EnemyBall だった場合は、自分にダメージ
						if (EnemyBall* myEnemy = dynamic_cast<EnemyBall*>(this))
						{
							myEnemy->TakeDamage(damageAmount);
						}

						// ② 相手(other) が EnemyBall だった場合は、相手にダメージ
						if (EnemyBall* otherEnemy = dynamic_cast<EnemyBall*>(other))
						{
							otherEnemy->TakeDamage(damageAmount);
						}
						// ==========================================================
					}
				}
			}
		}
	}
	else
	{
		// Groundが無い時の保険
		m_Transform.position += m_Velocity;
	}

	// ==========================
	// ポケットとの当たり判定
	// ==========================
	std::vector<Pocket*> pockets = Game::GetInstance()->GetObjects<Pocket>();

	for (Pocket* pocket : pockets)
	{
		if (Collision::CheckHit(GetSphere(), pocket->GetSphere()))
		{
			OnPocketHit();
			break;
		}
	}

	// ==========================
	// ボールの転がり回転の計算
	// ==========================
	if (m_OldPosition != m_Transform.position)
	{
		// 1フレームの実際の移動ベクトル
		Vector3 moveVec = m_Transform.position - m_OldPosition;

		// Y軸のブレによる影響を消すため、水平方向の移動のみを考慮
		moveVec.y = 0.0f;

		float distance = moveVec.Length();

		// 移動している場合のみ回転処理（ゼロ除算防止）
		if (distance > 0.0001f)
		{
			// 移動方向（正規化ベクトル）
			Vector3 moveDir = moveVec / distance;

			// 回転軸の計算（外積）
			// 進行方向(moveDir)と真上(UnitY)の外積をとることで、進行方向に対して「真横」の軸を取得
			Vector3 rotationAxis = Vector3::UnitY.Cross(moveDir);
			rotationAxis.Normalize();

			// 回転角の計算
			float angle = distance / m_Radius;

			// 指定した軸(rotationAxis)を中心に、指定した角度(angle)だけ回転するクォータニオン
			Quaternion deltaRot = Quaternion::CreateFromAxisAngle(rotationAxis, angle);

			// 5. 現在の転がり回転に掛け合わせる
			//m_RollingRotation = deltaRot * m_RollingRotation;
			m_RollingRotation = m_RollingRotation * deltaRot;
		}
	}
}

void BallBase::ResetToInitialPosition()
{
	// 位置情報を初期位置に戻す
	m_Transform.position = m_InitialPosition;
	m_Position = m_InitialPosition;
	m_OldPosition = m_InitialPosition;

	// 移動・加速度・転がり回転をリセットする
	m_Velocity = DirectX::SimpleMath::Vector3::Zero;
	m_Acceleration = DirectX::SimpleMath::Vector3::Zero;
	m_RollingRotation = DirectX::SimpleMath::Quaternion::Identity;
}

void BallBase::OnPocketHit()
{
	// デフォルトでは停止だけ
	m_Velocity = DirectX::SimpleMath::Vector3::Zero;
	m_Acceleration = DirectX::SimpleMath::Vector3::Zero;
}

void BallBase::LoadModel(const char* modelFilePath, const char* texDirectory)
{
	StaticMesh staticmesh;                         // 読み込み用の静的メッシュ
	staticmesh.Load(modelFilePath, texDirectory);

	m_MeshRenderer.Init(staticmesh);

	// 共通シェーダーを使う場合はここで指定
	m_Shader.Create("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");

	m_subsetList = staticmesh.GetSubsets();
	m_Textures = staticmesh.GetTextures();

	// マテリアルの登録処理
	std::vector<MATERIAL> materials = staticmesh.GetMaterials();
	for (const auto& matData : materials)
	{
		auto material = std::make_unique<Material>();
		material->Create(matData);
		m_Materials.push_back(std::move(material));
	}

	// 頂点座標からモデルの元の半径を自動計算
	float maxDist = 0.0f;
	for (const auto& vertex : staticmesh.GetVertices())
	{
		float dist = Vector3(vertex.position.x, vertex.position.y, vertex.position.z).Length();
		maxDist = (std::max)(maxDist, dist);
	}

	m_ModelBaseRadius = maxDist;    // モデル本来の半径を保存
	UpdateRadius();                 // スケールを掛けて m_Radius を更新
}

void BallBase::DrawMesh(const DirectX::SimpleMath::Matrix& worldMtx)
{
	Renderer::SetWorldMatrix(const_cast<DirectX::SimpleMath::Matrix*>(&worldMtx)); // GPUにセット

	//マテリアル数分ループ 
	for (int i = 0; i < static_cast<int>(m_subsetList.size()); i++)
	{
		// マテリアルをセット(サブセット情報の中にあるマテリアルインデックスを使用)
		m_Materials[m_subsetList[i].MaterialIdx]->SetGPU();

		if (m_Materials[m_subsetList[i].MaterialIdx]->isTextureEnable())
		{
			m_Textures[m_subsetList[i].MaterialIdx]->SetGPU();
		}

		m_MeshRenderer.DrawSubset(
			m_subsetList[i].IndexNum,       // 描画するインデックス数
			m_subsetList[i].IndexBase,      // 最初のインデックスバッファの位置	
			m_subsetList[i].VertexBase);    // 頂点バッファの最初から使用
	}
}

void BallBase::DrawImGui(const std::string& label)
{
	if (ImGui::CollapsingHeader(label.c_str()))
	{
		// スケール
		float currentScale = m_Transform.scale.x;

		if (ImGui::SliderFloat("Ball Scale", &currentScale, 0.1f, 5.0f))
		{
			// スライダーが動いたら、縦横奥（X, Y, Z）全てのスケールを均等に更新
			m_Transform.scale.x = currentScale;
			m_Transform.scale.y = currentScale;
			m_Transform.scale.z = currentScale;

			// スケールが変わったので、これに連動して物理半径 m_Radius を再計算する
			UpdateRadius();
		}

		// 質量
		ImGui::SliderFloat("Mass", &m_Mass, 0.1f, 10.0f);

		// 速度（読み取り専用で表示）
		ImGui::Text("Velocity: (%.2f, %.2f, %.2f)",
			m_Velocity.x, m_Velocity.y, m_Velocity.z);

		// 座標（読み取り専用で表示）
		ImGui::Text("Position: (%.2f, %.2f, %.2f)",
			m_Transform.position.x,
			m_Transform.position.y,
			m_Transform.position.z);

		// ステータス状態
		ImGui::Text("HP: %d / %d", m_HP, m_Status.maxHp);
		ImGui::Text("Attack: %d", m_Status.attack);
		ImGui::Text("Defense: %d", m_Status.defense);

		// 反発係数
		ImGui::SliderFloat("Restitution", &m_Restitution, 0.0f, 1.0f);

		// 摩擦係数
		ImGui::SliderFloat("Friction", &m_Friction, 0.0f, 1.0f);
	}
}

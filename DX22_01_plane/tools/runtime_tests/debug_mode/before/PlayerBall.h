#pragma once

#include "BallComponent.h"
#include "BallRenderComponent.h"
#include "Texture.h"
#include "MeshRenderer.h"
#include "StaticMesh.h"
#include "utility.h"
#include "Material.h"
#include <cstdint>
#include "Mesh.h"

#include <vector>
#include <memory>

class Camera;

//=======================================
// 軌跡の1点分の情報
//=======================================
struct TrailPoint
{
	DirectX::SimpleMath::Vector3 position;
	int timestamp;
	float lifeRatio;
};

//=======================================
// プレイヤーボール
//=======================================
class PlayerBall final : public Component
{
public:
	//=======================================
	// プレイヤー状態
	//=======================================
	enum class State
	{
		Simulation, // 物理演算中
		Idle,       // ショット待ち
		Goal,       // ゴール
		Dead        // 死亡
	};

public:
	//=======================================
	// 基本処理
	//=======================================
	void Awake() override;
	void Update() override;
	void FixedUpdate() override;
	void Draw() override;
	void OnDestroy() override;

	//=======================================
	// イベント処理
	//=======================================
	void Defeat();
	void OnPocketHit();
	void EnterPocketQueue();
	void ReturnFromPocket(
		const DirectX::SimpleMath::Vector3& position);

	//=======================================
	// 状態管理
	//=======================================
	void SetState(State state) { m_State = state; }
	State GetState() const { return m_State; }
	bool IsIdle() const { return !m_IsPocketed && m_State == State::Idle; }
	bool IsPocketed() const { return m_IsPocketed; }

	//=======================================
	// ステータス系
	//=======================================
	void TakeDamage(int damage);

	//=======================================
	// ショット・軌跡系
	//=======================================
	void Shot(DirectX::SimpleMath::Vector3 velocity) { m_Ball->GetMutableVelocity() = velocity; }
	void FireAutomatedShot(
		const DirectX::SimpleMath::Vector3& velocity);
	void ClearTrajectory() { m_TrajectoryPositions.clear(); }

	void SetStatus(const BallStatus& status)
	{
		m_Ball->SetStatus(status);
		m_PreTrajectoryDirty = true;
	}
	const BallStatus& GetStatus() const { return m_Ball->GetStatus(); }
	void SetHP(int hp) { m_Ball->SetHP(hp); }
	void SetMaxHP(int maxHp) { m_Ball->SetMaxHP(maxHp); }
	int GetHP() const { return m_Ball->GetHP(); }
	int GetMaxHP() const { return m_Ball->GetMaxHP(); }
	int GetAttack() const { return m_Ball->GetAttack(); }
	int GetDefense() const { return m_Ball->GetDefense(); }
	int CalculateDamageTaken(int damage) const
	{
		return m_Ball->CalculateDamageTaken(damage);
	}
	bool IsDefeated() const { return m_Ball->IsDefeated(); }
	bool IsStopped() const { return m_Ball->IsStopped(); }
	DirectX::SimpleMath::Vector3 GetVelocity() const { return m_Ball->GetVelocity(); }
	DirectX::SimpleMath::Vector3 GetPosition() const { return m_Ball->GetPosition(); }
	BallComponent* GetBall() const { return m_Ball; }

	//=======================================
	// 弾道予測系
	//=======================================
	void GeneratePreTrajectory(const DirectX::SimpleMath::Vector3& initialVelocity);


	//=======================================
	// デバッグUI
	//=======================================
	void DrawImGui();

private:
	void Init();
	void Draw(Camera* cam);
	void Uninit();

	void LoadModel(const char* modelFilePath, const char* textureDirectory)
	{
		m_RenderComponent->LoadModel(modelFilePath, textureDirectory);
		m_Ball->UpdateRadius();
	}
	void SetInitialPosition(const DirectX::SimpleMath::Vector3& position)
	{
		m_Ball->SetInitialPosition(position);
	}
	void UpdateRadius() { m_Ball->UpdateRadius(); }
	void DrawMesh(const DirectX::SimpleMath::Matrix& worldMatrix)
	{
		m_RenderComponent->DrawMesh(worldMatrix);
	}
	void Damage(int damage) { m_Ball->Damage(damage); }

	//=======================================
	// Update内部処理
	//=======================================
	void UpdateSimulation();
	void UpdateDebugMove();
	void UpdateStopByFriction();
	void CheckFallRespawn();

	//=======================================
	// 軌跡更新
	//=======================================
	void AddTrailPoint();
	void UpdateTrailLife();

	//=======================================
	// マウスエイム・ショット操作
	//=======================================
	void UpdateAim();
	bool TryGetMouseAimPosition(DirectX::SimpleMath::Vector3& aimPosition) const;
	void UpdateAimDirectionFromMouse();
	void BeginMousePowerDrag();
	void UpdateShotPowerFromMouseDrag();
	void CancelMousePowerDrag();
	void FireMouseShot();
	DirectX::SimpleMath::Vector3 GetShotVector() const;

	//=======================================
	// 弾道予測の描画
	//=======================================
	void InitTrajectoryVisualModel();
	void DrawTrajectoryLine();

	void DrawGuideSegment(
		const DirectX::SimpleMath::Vector3& start,
		const DirectX::SimpleMath::Vector3& end,
		float thickness,
		float yOffset,
		int materialIndex);

	void DrawGuideCircle(
		const DirectX::SimpleMath::Vector3& center,
		float radius,
		float thickness,
		float yOffset,
		int materialIndex);

private:
	BallComponent* m_Ball = nullptr;
	BallRenderComponent* m_RenderComponent = nullptr;
	bool m_IsPocketed = false;

	//=======================================
	// 弾道予測用の簡易メッシュ
	//=======================================
	class PreviewMesh : public Mesh
	{
	public:
		void InitQuad()
		{
			m_vertices =
			{
				{
					DirectX::SimpleMath::Vector3(-0.5f, 0.0f, -0.5f),
					DirectX::SimpleMath::Vector3(0.0f, 1.0f, 0.0f),
					DirectX::SimpleMath::Color(1.0f, 1.0f, 1.0f, 1.0f),
					DirectX::SimpleMath::Vector2(0.0f, 1.0f)
				},
				{
					DirectX::SimpleMath::Vector3(0.5f, 0.0f, -0.5f),
					DirectX::SimpleMath::Vector3(0.0f, 1.0f, 0.0f),
					DirectX::SimpleMath::Color(1.0f, 1.0f, 1.0f, 1.0f),
					DirectX::SimpleMath::Vector2(1.0f, 1.0f)
				},
				{
					DirectX::SimpleMath::Vector3(0.5f, 0.0f, 0.5f),
					DirectX::SimpleMath::Vector3(0.0f, 1.0f, 0.0f),
					DirectX::SimpleMath::Color(1.0f, 1.0f, 1.0f, 1.0f),
					DirectX::SimpleMath::Vector2(1.0f, 0.0f)
				},
				{
					DirectX::SimpleMath::Vector3(-0.5f, 0.0f, 0.5f),
					DirectX::SimpleMath::Vector3(0.0f, 1.0f, 0.0f),
					DirectX::SimpleMath::Color(1.0f, 1.0f, 1.0f, 1.0f),
					DirectX::SimpleMath::Vector2(0.0f, 0.0f)
				}
			};

			// 両面描画用
			m_indices =
			{
				0, 2, 1,
				0, 3, 2,
				0, 1, 2,
				0, 2, 3
			};
		}
	};

private:
	//=======================================
	// 状態管理
	//=======================================
	State m_State = State::Idle;
	int m_StopCount = 0;
	bool m_SkipSimulationOnNextFixedUpdate = false;
	DirectX::SimpleMath::Vector3 m_DebugMoveInput = DirectX::SimpleMath::Vector3::Zero;

	//=======================================
	// フレーム管理
	//=======================================
	int m_CurrentFrame = 0;
	const int TRAIL_DURATION_FRAMES = 60;

	//=======================================
	// 軌跡
	//=======================================
	std::vector<TrailPoint> m_TrajectoryPositions;
	DirectX::SimpleMath::Vector3 m_LastTrailPos;

	//=======================================
	// 弾道予測データ
	//=======================================
	std::vector<TrailPoint> m_PrePositions;

	bool m_PreTrajectoryDirty = true;
	int m_PreviewRefreshFramesRemaining = 0;
	float m_LastPreviewAimAngle = 99999.0f;
	float m_LastPreviewShotPower = -1.0f;

	DirectX::SimpleMath::Vector3 m_LastPreviewPosition =
		DirectX::SimpleMath::Vector3(99999.0f, 99999.0f, 99999.0f);

	std::uint64_t m_LastPreviewWorldKey = 0;
	bool m_PreviewComplete = false;

	//=======================================
	// 弾道予測描画
	//=======================================
	PreviewMesh m_PreviewMesh;
	MeshRenderer m_PreviewMeshRenderer;
	std::vector<std::unique_ptr<Material>> m_PreviewMaterials;
	std::vector<SUBSET> m_PreviewSubsets;

	bool m_PreviewHitBall = false;

	DirectX::SimpleMath::Vector3 m_PreviewGhostBallPosition =
		DirectX::SimpleMath::Vector3::Zero;

	DirectX::SimpleMath::Vector3 m_PreviewHitBallPosition =
		DirectX::SimpleMath::Vector3::Zero;

	DirectX::SimpleMath::Vector3 m_PreviewObjectBallDirection =
		DirectX::SimpleMath::Vector3::Zero;

	//=======================================
	// エイム・ショット入力
	//=======================================
	float m_AimAngle = 0.0f;
	float m_ShotPower = 5.0f;

	const float m_MinShotPower = 1.0f;
	const float m_MaxShotPower = 8.0f;
	const float m_PowerStep = 0.1f;

	bool m_IsPowerDragging = false;
	float m_LockedAimAngle = 0.0f;

	DirectX::SimpleMath::Vector3 m_LockedShotDirection =
		DirectX::SimpleMath::Vector3::UnitZ;

	DirectX::XMFLOAT2 m_PowerDragStartMousePos{};

	const float m_PixelsForMaxShotPower = 300.0f;
	const float m_PowerPreviewStep = 0.1f;
};

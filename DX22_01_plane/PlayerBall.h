#pragma once

#include "BallBase.h"
#include "Texture.h"
#include "MeshRenderer.h"
#include "StaticMesh.h"
#include "utility.h"
#include "Material.h"
#include "TrajectoryModel.h"
#include "Mesh.h"

#include <vector>
#include <memory>

//=======================================
// 弾道予測の描画モデルタイプ
//=======================================
enum class TrajectoryVisualModel
{
	Sphere,
	Cylinder,
	Quad
};

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
class PlayerBall : public BallBase
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
	void Init() override;
	void Update() override;
	void Draw(Camera* cam) override;
	void Uninit() override;

	//=======================================
	// イベント処理
	//=======================================
	void Defeat() override;
	void OnPocketHit() override;

	//=======================================
	// 状態管理
	//=======================================
	void SetState(State state) { m_State = state; }
	State GetState() const { return m_State; }
	bool IsIdle() const { return m_State == State::Idle; }

	//=======================================
	// ステータス系
	//=======================================
	void TakeDamage(int damage) override;

	//=======================================
	// ショット・軌跡系
	//=======================================
	void Shot(DirectX::SimpleMath::Vector3 velocity) { m_Velocity = velocity; }
	void ClearTrajectory() { m_TrajectoryPositions.clear(); }

	//=======================================
	// 弾道予測系
	//=======================================
	void GeneratePreTrajectory(const DirectX::SimpleMath::Vector3& initialVelocity);

	void SetTrajectoryModel(std::unique_ptr<ITrajectoryModel> model)
	{
		m_TrajectoryModel = std::move(model);
	}

	void SetTrajectoryVisualModel(TrajectoryVisualModel model)
	{
		m_TrajectoryVisualModel = model;
	}

	TrajectoryVisualModel GetTrajectoryVisualModel() const
	{
		return m_TrajectoryVisualModel;
	}

	//=======================================
	// デバッグUI
	//=======================================
	void DrawImGui();

private:
	//=======================================
	// Update内部処理
	//=======================================
	void UpdateSimulation();
	void UpdateDebugMove();
	void UpdateStopByFriction();
	void CheckFallRespawn();
	void CheckCupIn();

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
	float m_LastPreviewAimAngle = 99999.0f;
	float m_LastPreviewShotPower = -1.0f;

	DirectX::SimpleMath::Vector3 m_LastPreviewPosition =
		DirectX::SimpleMath::Vector3(99999.0f, 99999.0f, 99999.0f);

	std::unique_ptr<ITrajectoryModel> m_TrajectoryModel;
	TrajectoryVisualModel m_TrajectoryVisualModel = TrajectoryVisualModel::Quad;

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
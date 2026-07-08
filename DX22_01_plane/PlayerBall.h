#pragma once

#include "BallBase.h"
#include "Texture.h"
#include "MeshRenderer.h"
#include "StaticMesh.h"
#include "utility.h"
#include "Material.h"
#include "TrajectoryModel.h"  // ★ 追加
#include "Mesh.h"

#include<vector>
#include<memory>

// 弾道予測の描画モデルタイプ
enum class TrajectoryVisualModel
{
	Sphere,      // スフィア（現在のGolfBall）
	Cylinder,    // シリンダー（ラインのような見た目）
	Quad         // クワッド（平面 - 最もシンプル）
};

struct TrailPoint {
	DirectX::SimpleMath::Vector3 position;
	int timestamp;//軌跡が生成されたフレーム番号

	float lifeRatio;	//0.0(消滅)～1.0(出現直後)
};

class PlayerBall :public BallBase
{
public:
	// プレイヤーの状態を明確に定義（列挙型）
	enum class State {
		Simulation,     // 0: 物理演算中（転がっている状態）
		Idle,           // 1: 停止中（ショット待ち状態）
		Goal,           // 2: カップイン / ゴール
		Dead            // 3: 落下・死亡（必要に応じて拡張）
	};

private:
	State m_State = State::Idle;
	int m_StopCount = 0; // 停止カウント

	// 軌跡用変数
	std::vector<TrailPoint>m_TrajectoryPositions;//過去座標	
	DirectX::SimpleMath::Vector3 m_LastTrailPos;// 最後に点を打った場所を記録する変数

	// 予測弾道用
	std::vector<TrailPoint> m_PrePositions;

	// 全体フレームカウンター
	int m_CurrentFrame = 0;

	// 軌跡の表示時間（フレーム数）
	const int TRAIL_DURATION_FRAMES = 60;

	// 弾道予測の再計算制御
	bool m_PreTrajectoryDirty = true;
	float m_LastPreviewAimAngle = 99999.0f;
	float m_LastPreviewShotPower = -1.0f;
	DirectX::SimpleMath::Vector3 m_LastPreviewPosition =
		DirectX::SimpleMath::Vector3(99999.0f, 99999.0f, 99999.0f);

	// 弾道予測用
	std::unique_ptr<ITrajectoryModel> m_TrajectoryModel;

	// 弾道予測用の描画モデル選択
	TrajectoryVisualModel m_TrajectoryVisualModel = TrajectoryVisualModel::Quad;

	// Mesh継承して簡易実装
	class PreviewMesh : public Mesh {
	public:
		void InitQuad() {
			// クワッド（平面）の頂点を生成
			m_vertices = {
				{ DirectX::SimpleMath::Vector3(-0.5f, 0.0f, -0.5f), DirectX::SimpleMath::Vector3(0.0f, 1.0f, 0.0f), DirectX::SimpleMath::Color(1.0f, 1.0f, 1.0f, 1.0f), DirectX::SimpleMath::Vector2(0.0f, 1.0f) },
				{ DirectX::SimpleMath::Vector3(0.5f, 0.0f, -0.5f), DirectX::SimpleMath::Vector3(0.0f, 1.0f, 0.0f), DirectX::SimpleMath::Color(1.0f, 1.0f, 1.0f, 1.0f), DirectX::SimpleMath::Vector2(1.0f, 1.0f) },
				{ DirectX::SimpleMath::Vector3(0.5f, 0.0f, 0.5f), DirectX::SimpleMath::Vector3(0.0f, 1.0f, 0.0f), DirectX::SimpleMath::Color(1.0f, 1.0f, 1.0f, 1.0f), DirectX::SimpleMath::Vector2(1.0f, 0.0f) },
				{ DirectX::SimpleMath::Vector3(-0.5f, 0.0f, 0.5f), DirectX::SimpleMath::Vector3(0.0f, 1.0f, 0.0f), DirectX::SimpleMath::Color(1.0f, 1.0f, 1.0f, 1.0f), DirectX::SimpleMath::Vector2(0.0f, 0.0f) }
			};

			// インデックスを生成
			// Back-face culling can hide a flat guide quad, so draw both sides.
			m_indices = { 0, 2, 1, 0, 3, 2, 0, 1, 2, 0, 2, 3 };
		}
	};

	// 弾道予測用の別MeshRenderer
	PreviewMesh m_PreviewMesh;
	MeshRenderer m_PreviewMeshRenderer;
	std::vector<std::unique_ptr<Material>> m_PreviewMaterials;
	std::vector<SUBSET> m_PreviewSubsets;

	bool m_PreviewHitBall = false;
	DirectX::SimpleMath::Vector3 m_PreviewGhostBallPosition = DirectX::SimpleMath::Vector3::Zero;
	DirectX::SimpleMath::Vector3 m_PreviewHitBallPosition = DirectX::SimpleMath::Vector3::Zero;
	DirectX::SimpleMath::Vector3 m_PreviewObjectBallDirection = DirectX::SimpleMath::Vector3::Zero;

	// 弾道予測用モデルの初期化・描画
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


	// --- Arrow 機能統合（旧 Arrow クラスの変数を PlayerBall に移管）---
	float m_AimAngle = 0.0f;				// エイム方向（ラジアン）
	float m_ShotPower = 5.0f;				// 現在のショットパワー
	const float m_MinShotPower = 1.0f;		// パワー下限
	const float m_MaxShotPower = 8.0f;		// パワー上限
	const float m_PowerStep = 0.1f;			// 1フレームあたりのパワー変化量
	bool m_IsPowerDragging = false;			// パワー調整ドラッグ中かを保持する
	float m_LockedAimAngle = 0.0f;			// クリック時に固定したショット角度
	DirectX::SimpleMath::Vector3 m_LockedShotDirection = DirectX::SimpleMath::Vector3::UnitZ; // 固定したショット方向
	DirectX::XMFLOAT2 m_PowerDragStartMousePos{};	// パワー計算の基準になるドラッグ開始位置
	const float m_PixelsForMaxShotPower = 300.0f;	// 最大パワーに到達するドラッグ距離
	const float m_PowerPreviewStep = 0.1f;			// 予測線の揺れを抑えるパワー更新刻み

	// 旧 Arrow::Update() + Arrow::SetState() 相当
	void UpdateAim();

	bool TryGetMouseAimPosition(DirectX::SimpleMath::Vector3& aimPosition) const; // マウス位置を床面上の狙い位置に変換する
	void UpdateAimDirectionFromMouse();			// ボールからマウス位置への方向にエイムを更新する
	void BeginMousePowerDrag();					// 左クリック時の方向を固定してパワー調整を開始する
	void UpdateShotPowerFromMouseDrag();		// ドラッグ距離からショットパワーを更新する
	void CancelMousePowerDrag();				// 右クリックでパワー調整を中止する
	void FireMouseShot();						// 固定方向と現在パワーでショットを実行する
	DirectX::SimpleMath::Vector3 GetShotVector() const; // 状態に応じたショット速度ベクトルを取得する

public:
	void Init()override;
	void Update()override;
	void Draw(Camera* cam)override;
	void Uninit()override;

	void Defeat() override;

	void OnPocketHit() override;

	// 状態の設定・取得
	void SetState(State state) { m_State = state; }
	State GetState() const { return m_State; }
	bool IsIdle() const { return m_State == State::Idle; }
	void TakeDamage(int damage) override;

	// モデル選択用メソッドを追加
	void SetTrajectoryModel(std::unique_ptr<ITrajectoryModel> model)
	{
		m_TrajectoryModel = std::move(model);
	}

	// 弾道予測モデルの選択
	void SetTrajectoryVisualModel(TrajectoryVisualModel model)
	{
		m_TrajectoryVisualModel = model;
	}
	TrajectoryVisualModel GetTrajectoryVisualModel() const { return m_TrajectoryVisualModel; }


	//弾道予測を生成する関数
	void GeneratePreTrajectory(const DirectX::SimpleMath::Vector3& initialVelocity);

	// ショット
	void Shot(DirectX::SimpleMath::Vector3 v) { m_Velocity = v; }

	// リセット用関数
	void ClearTrajectory() { m_TrajectoryPositions.clear(); }

	//void SetGround(Ground* ground);

	// ImGui描画用の関数
	void DrawImGui();
};


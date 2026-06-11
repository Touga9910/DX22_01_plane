#pragma once

#include "BallBase.h"
#include "Texture.h"
#include "MeshRenderer.h"
#include "StaticMesh.h"
#include "utility.h"
#include "Material.h"

#include<vector>

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

public:
	void Init();
	void Update();
	void Draw(Camera* cam);
	void Uninit();

	// 状態の設定・取得
	void SetState(State state) { m_State = state; }
	State GetState() const { return m_State; }
	bool IsIdle() const { return m_State == State::Idle; }

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


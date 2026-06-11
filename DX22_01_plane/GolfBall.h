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

class GolfBall :public BallBase
{
private:
	int m_State = 0; // 状態 0：物理挙動,1:停止,2:カップイン
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
	void SetState(int s) { m_State = s; }
	int GetState() const { return m_State; }

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


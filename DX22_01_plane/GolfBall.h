#pragma once

#include "Object.h"
#include "Texture.h"
#include "MeshRenderer.h"
#include "StaticMesh.h"
#include "utility.h"
#include "Material.h"

#include<vector>
//#include "Ground.h"

struct TrailPoint {
	DirectX::SimpleMath::Vector3 position;
	int timestamp;//軌跡が生成されたフレーム番号

	float lifeRatio;	//0.0(消滅)～1.0(出現直後)
};

class GolfBall :public Object
{
private:

	//速度
	DirectX::SimpleMath::Vector3 m_Velocity = DirectX::SimpleMath::Vector3(0.0f, 0.0f, 0.0f);

	//加速度
	DirectX::SimpleMath::Vector3 m_Acceleration = DirectX::SimpleMath::Vector3(0.0f, 0.0f, 0.0f);

	// 描画の為の情報（メッシュに関わる情報）
	MeshRenderer m_MeshRenderer; // 頂点バッファ・インデックスバッファ・インデックス数

	// 描画の為の情報（見た目に関わる部分）
	std::vector<std::unique_ptr<Material>> m_Materials;
	std::vector<SUBSET> m_subsets;
	std::vector<std::unique_ptr<Texture>> m_Textures; // テクスチャ

	int m_State = 0; // 状態 0：物理挙動,1:停止,2:カップイン
	int m_StopCount = 0; // 停止カウント

	//Ground* m_Ground;	//地面オブジェクト

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
};


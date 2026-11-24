#pragma once

#include	<SimpleMath.h>
#include "RenderViewOverride.h"
//#include "input.h"

//-----------------------------------------------------------------------------
//Cameraクラス
//-----------------------------------------------------------------------------
class Camera {
private:
	DirectX::SimpleMath::Vector3	m_Position = DirectX::SimpleMath::Vector3(0.0f, 0.0f, 0.0f);
	DirectX::SimpleMath::Vector3	m_Target{};
	DirectX::SimpleMath::Matrix		m_ViewMatrix{};

	// カメラの移動関連の変数
	float m_CameraDirection = 0;	// カメラの角度
	float m_CameraDistanceY = 150;	// カメラのY座標の距離
	float m_TargetDistanceY = 150;	// カメラが向かってほしい目的地（Lerp実装で徐々に近づけるため）

	// ズーム関連の変数
	static constexpr float ZOOM_SPEED = 2.0f;					// ズームの速さ
    static constexpr float MIN_DISTANCE = 30.0f;				// ズームの限界（最近地点）
    static constexpr float MAX_DISTANCE = 500.0f;				// ズームの限界（最遠地点）
    static constexpr float ZOOM_INTERPOLATION_SPEED = 0.1f;		// ズームの適応速度（0.0f～1.0fの範囲で指定）

	//シングルトン構成化
	Camera();
	~Camera();

	/// <summary>
	/// カメラのコピー生成を禁止する。
	/// </summary>
	Camera(const Camera&) = delete;
	/// <summary>
	/// カメラのコピー代入を禁止する。
	/// </summary>
	Camera& operator=(const Camera&) = delete;



public:
	/// <summary>
	/// 共有カメラを初回の呼び出しで生成し、その参照を返す。
	/// </summary>
	static Camera& GetInstance()
	{
		static Camera instance;
		return instance;
	}

	void Init();
	void Update();
	/// <summary>
	/// 指定位置を見下ろすようにカメラを更新する。
	/// </summary>
	/// <param name="targetPos">追従対象の位置</param>
	void Update(const DirectX::SimpleMath::Vector3& targetPos);
	void SetCamera(int mode = 0);
	void Uninit();
	/// <summary>
	/// カメラの位置と注視点からビュー行列を計算する。
	/// </summary>
	void UpdateViewMatrix();

	/// <summary>
	/// 描画時の視点の上書き設定があればその位置を返し、それ以外はカメラの現在位置を返す。
	/// </summary>
	DirectX::SimpleMath::Vector3 GetPosition() { return RenderViewOverride::current ? RenderViewOverride::current->eye : m_Position; }
	DirectX::SimpleMath::Vector3 GetTarget() const{	return m_Target; }
	DirectX::SimpleMath::Matrix GetViewMatrix() const { return m_ViewMatrix; }

	void SetTarget(DirectX::SimpleMath::Vector3 target);

	/// <summary>
	/// カメラの現在の水平角を返す。
	/// </summary>
	float GetCameraDirection() { return m_CameraDirection; }

	/// <summary>
	/// 注視点からのカメラの高さを設定する。
	/// </summary>
	/// <param name="dist">注視点からの高さ</param>
	void SetDistanceY(float dist);

	/// <summary>
	/// カメラ位置の更新処理
	/// </summary>
	void RefreshPosition();
};
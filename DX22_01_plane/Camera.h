#pragma once

#include	<SimpleMath.h>
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

	Camera(const Camera&) = delete;
	Camera& operator=(const Camera&) = delete;



public:
	static Camera& GetInstance()
	{
		static Camera instance;
		return instance;
	}

	void Init();
	void Update();
	/// <summary>
	/// 追従カメラの更新
	/// </summary>
	/// <param name="targetPos">対象オブジェクト</param>
	void Update(const DirectX::SimpleMath::Vector3& targetPos);
	void SetCamera(int mode = 0);
	void Uninit();
	/// <summary>
	/// カメラ追従のあれこれ
	/// </summary>
	void UpdateViewMatrix();

	DirectX::SimpleMath::Vector3 GetPosition() { return m_Position; }
	DirectX::SimpleMath::Matrix GetViewMatrix() const { return m_ViewMatrix; }

	void SetTarget(DirectX::SimpleMath::Vector3 target);

	float GetCameraDirection() { return m_CameraDirection; }

	/// <summary>
	/// カメラの高さを個別に設定する
	/// </summary>
	/// <param name="dist">カメラの距離</param>
	void SetDistanceY(float dist);

	/// <summary>
	/// カメラ位置の更新処理
	/// </summary>
	void RefreshPosition();
};
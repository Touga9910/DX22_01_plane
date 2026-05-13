#pragma once

#include	<SimpleMath.h>
#include "input.h"

//-----------------------------------------------------------------------------
//Cameraクラス
//-----------------------------------------------------------------------------
class Camera {
private:
	DirectX::SimpleMath::Vector3	m_Position = DirectX::SimpleMath::Vector3(0.0f, 0.0f, 0.0f);
	DirectX::SimpleMath::Vector3	m_Rotation = DirectX::SimpleMath::Vector3(0.0f, 0.0f, 0.0f);
	DirectX::SimpleMath::Vector3	m_Scale = DirectX::SimpleMath::Vector3(1.0f, 1.0f, 1.0f);

	DirectX::SimpleMath::Vector3	m_Target{};
	DirectX::SimpleMath::Matrix		m_ViewMatrix{};

	DirectX::SimpleMath::Matrix m_View;


	float m_CameraDirection = 0;	// カメラの角度
	float m_CameraDistanceY = 150;	// カメラとターゲットの距離

	//シングルトン構成化
	Camera();
	~Camera();

	Camera(const Camera&) = delete;
	Camera& operator=(const Input&) = delete;



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

	void SetTarget(DirectX::SimpleMath::Vector3 target);

	float GetCameraDirection() { return m_CameraDirection; }

	/// <summary>
	/// カメラの高さを個別に設定する
	/// </summary>
	/// <param name="dist">カメラの距離</param>
	void SetDistanceY(float dist);
};
#include "Renderer.h"
#include "Camera.h"
#include "Application.h"
#include "input.h"

using namespace DirectX::SimpleMath;

namespace
{
	constexpr float CameraRadius = 50.0f;	//カメラの回転時半径
}

//コンストラクタ
Camera::Camera()
{
	//必要ならば初期化処理を
}

//デストラクタ
Camera::~Camera()
{
}

//=======================================
//初期化処理
//=======================================
void Camera::Init()
{
	m_Position = Vector3(0.0f, 20.0f, -50.0f);	// カメラがどの位置にいるのか （X,Y,Z）
	m_Target = Vector3(0.0f, 0.0f, 0.0f);		// カメラがどの座標に向いているのか ＝ 中心点がどこなのか（X,Y,Z）
	m_CameraDirection = 3.14f;

	m_CameraDistanceY = 150.0f;
	m_TargetDistanceY = 150.0f;

}


//=======================================
//更新処理
//=======================================
void Camera::Update()
{

	if (Input::GetKeyPress(VK_O)) // Oキーでズームイン（カメラを下げる）
	{
		m_TargetDistanceY -= ZOOM_SPEED;
	}
	if (Input::GetKeyPress(VK_P)) // Pキーでズームアウト（カメラを上げる）
	{
		m_TargetDistanceY += ZOOM_SPEED;
	}

	if (m_TargetDistanceY < MIN_DISTANCE) m_TargetDistanceY = MIN_DISTANCE;
	if (m_TargetDistanceY > MAX_DISTANCE) m_TargetDistanceY = MAX_DISTANCE;

	// 現在の高さ から 目標の高さ へ、指定した割合だけ毎フレーム近づける
	m_CameraDistanceY = m_CameraDistanceY + (m_TargetDistanceY - m_CameraDistanceY) * ZOOM_INTERPOLATION_SPEED;

	//オブジェクトとカメラの編集を同時に行わない ＝ どちらかを編集するときはもう片方は触らない
	RefreshPosition();	// カメラの位置を更新
	UpdateViewMatrix(); // ビュー行列の更新
}

//=======================================
//更新処理(追従)
//=======================================
void Camera::Update(const Vector3& targetPos)
{
	// 視線はキャラクターに向ける
	m_Target = targetPos;

	// 位置の計算は引数なしのUpdate()に任せる（処理の共通化）
	Update();

	// ※もしUpdateViewMatrixがここで必要な場合は残します
	UpdateViewMatrix();
}

//=======================================
//描画処理
//=======================================
void Camera::SetCamera(int mode)
{
	//3Dカメラ設定
	if (mode == 0)
	{
		// ビュー変換行列作成
		Renderer::SetViewMatrix(&m_ViewMatrix);

		//プロジェクション行列の生成
		constexpr float fieldOfView = DirectX::XMConvertToRadians(45.0f);    // 視野角 （上げると視野が広がる ＝ オブジェクトのサイズが小さく見えたりする）

		float aspectRatio = static_cast<float>(Application::GetWidth()) / static_cast<float>(Application::GetHeight());	// アスペクト比	
		float nearPlane = 1.0f;       // ニアクリップ
		float farPlane = 1000.0f;      // ファークリップ

		//プロジェクション行列の生成
		Matrix projectionMatrix;
		projectionMatrix = DirectX::XMMatrixPerspectiveFovLH(fieldOfView, aspectRatio, nearPlane, farPlane);	//左手系

		Renderer::SetProjectionMatrix(&projectionMatrix);
	}
	//2Dカメラ設定
	else if (mode == 1)
	{
		// ビュー変換行列作成
		Vector3 pos = { 0.0f, 0.0f, -10.0f };
		Vector3 tgt = {0.0f, 0.0f, 1.0f};
		Vector3 up = Vector3(0.0f, 1.0f, 0.0f);
		m_ViewMatrix = DirectX::XMMatrixLookAtLH(pos, tgt, up); //左手系

		Renderer::SetViewMatrix(&m_ViewMatrix);

		//プロジェクション行列の生成
		float nearPlane = 1.0f;       // ニアクリップ
		float farPlane = 1000.0f;     // ファークリップ

		//プロジェクション行列の生成
		Matrix projectionMatrix;
		projectionMatrix = DirectX::XMMatrixOrthographicLH(
			static_cast<float>(Application::GetWidth()),
			static_cast<float>(Application::GetHeight()),
			nearPlane, farPlane);

		projectionMatrix = DirectX::XMMatrixTranspose(projectionMatrix);
		Renderer::SetProjectionMatrix(&projectionMatrix);
	}
}


//=======================================
//終了処理
//=======================================
void Camera::Uninit()
{

}

//=======================================
//カメラ追従の設定
//=======================================
void Camera::UpdateViewMatrix()
{
	Vector3 up = Vector3(0.0f, 1.0f, 0.0f);
	m_ViewMatrix = DirectX::XMMatrixLookAtLH(m_Position, m_Target, up);
}

//カメラのターゲットを設定
void Camera::SetTarget(DirectX::SimpleMath::Vector3 target)
{
	//カメラの注視点を更新
	m_Target = target;

	RefreshPosition();
}

void Camera::SetDistanceY(float dist)
{
	m_CameraDistanceY = dist;
	RefreshPosition();

}

void Camera::RefreshPosition()
{
	m_Position = m_Target + Vector3(0.0f, m_CameraDistanceY, -0.1f);
}	
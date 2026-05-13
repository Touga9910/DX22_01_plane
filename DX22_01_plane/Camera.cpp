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

}


//=======================================
//更新処理
//=======================================
void Camera::Update()
{
	//オブジェクトとカメラの編集を同時に行わない ＝ どちらかを編集するときはもう片方は触らない

	//左右キーでカメラ回転
	/*
	if (Input::GetKeyPress(VK_D))
	{
		m_CameraDirection += 0.03f;
	}
	if (Input::GetKeyPress(VK_A))
	{
		m_CameraDirection -= 0.03f;
	}
	*/

	//カメラの位置を更新
	Vector3 pos = m_Target;
	/*
	pos.x += sin(m_CameraDirection) * CameraRadius;
	pos.y += 20;
	pos.z += cos(m_CameraDirection) * CameraRadius;
	*/
	pos.x += 0.0f;
	pos.y += m_CameraDistanceY;  // カメラの高さ（画面に収まるように数値を調整してください）
	pos.z -= 0.1f;    // 完全に真上だとLookAt行列の計算が破綻するため、Zをわずかにずらす

	m_Position = pos;
}

//=======================================
//更新処理(追従)
//=======================================
void Camera::Update(const Vector3& targetPos)
{
	/*
	// キャラクターの背後にカメラを配置する
	m_Position = targetPos + Vector3(0, 30, -50); // 上に30、後ろに50
	*/
	// キャラクターの真上にカメラを配置する（見下ろし固定）
	// Y座標を高くし、Z座標をわずかにずらして計算破綻を防ぐ
	m_Position = targetPos + Vector3(0.0f, m_CameraDistanceY, -0.1f);
	m_Target = targetPos; // 視線はキャラクターに向ける
	
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
		Vector3 up = Vector3(0.0f, 1.0f, 0.0f);
		m_ViewMatrix = DirectX::XMMatrixLookAtLH(m_Position, m_Target, up); //左手系

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
		float farPlane = 1000.0f;      // ファークリップ

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
	m_View = Matrix::CreateLookAt(m_Position, m_Target, Vector3::UnitY);
}

//カメラのターゲットを設定
void Camera::SetTarget(DirectX::SimpleMath::Vector3 target)
{
	//カメラの注視点を更新
	m_Target = target;

	m_Position = m_Target + DirectX::SimpleMath::Vector3(0.0f, m_CameraDistanceY, -0.1f);
}

void Camera::SetDistanceY(float dist)
{
	m_CameraDistanceY = dist;
	m_Position = m_Target + Vector3(0.0f, m_CameraDistanceY, -0.1f);

}

#include "Renderer.h"
#include "Camera.h"
#include "Application.h"
#include "input.h"
#include "imgui/imgui.h"

using namespace DirectX::SimpleMath;

namespace
{
	constexpr float CameraRadius = 50.0f;	//カメラの回転時半径
}

/// <summary>
/// カメラを生成し、各メンバーを宣言時の初期値で初期化する。
/// </summary>
Camera::Camera()
{
	//必要ならば初期化処理を
}

/// <summary>
/// カメラを破棄する。
/// </summary>
Camera::~Camera()
{
}

/// <summary>
/// カメラの初期位置、注視点、水平角と高さを設定する。
/// </summary>
void Camera::Init()
{
	m_Position = Vector3(0.0f, 20.0f, -50.0f);	// カメラがどの位置にいるのか （X,Y,Z）
	m_Target = Vector3(0.0f, 0.0f, 0.0f);		// カメラがどの座標に向いているのか ＝ 中心点がどこなのか（X,Y,Z）
	m_CameraDirection = 3.14f;

	m_CameraDistanceY = 150.0f;
	m_TargetDistanceY = 150.0f;

}


/// <summary>
/// 現在の注視点を基準にカメラの位置を更新し、ビュー行列を再計算する。
/// </summary>
void Camera::Update()
{
	RefreshPosition();	// カメラの位置を更新
	UpdateViewMatrix(); // ビュー行列の更新
}

/// <summary>
/// 指定位置を注視点に設定し、カメラの位置とビュー行列を更新する。
/// </summary>
void Camera::Update(const Vector3& targetPos)
{
	// 視線はキャラクターに向ける
	m_Target = targetPos;

	// 位置の計算は引数なしのUpdate()に任せる（処理の共通化）
	Update();

	// ※もしUpdateViewMatrixがここで必要な場合は残します
	UpdateViewMatrix();
}

/// <summary>
/// 2Dまたは3D描画のビュー行列と射影行列を設定し、3D描画では指定された上書き設定を優先する。
/// </summary>
void Camera::SetCamera(int mode)
{
    if (mode == 0 && RenderViewOverride::current)
    {
        auto view = RenderViewOverride::current->view;
        auto projection = RenderViewOverride::current->projection;
        Renderer::SetViewMatrix(&view);
        Renderer::SetProjectionMatrix(&projection);
        return;
    }
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


/// <summary>
/// ゲーム終了時のカメラの後処理として呼び出される。
/// </summary>
void Camera::Uninit()
{

}

/// <summary>
/// カメラの位置と注視点から左手系のビュー行列を計算する。
/// </summary>
void Camera::UpdateViewMatrix()
{
	Vector3 up = Vector3(0.0f, 1.0f, 0.0f);
	m_ViewMatrix = DirectX::XMMatrixLookAtLH(m_Position, m_Target, up);
}

/// <summary>
/// 注視点を設定し、その点を見下ろす位置にカメラを移動する。
/// </summary>
void Camera::SetTarget(DirectX::SimpleMath::Vector3 target)
{
	//カメラの注視点を更新
	m_Target = target;

	RefreshPosition();
}

/// <summary>
/// 注視点からの高さを設定し、見下ろしカメラの位置を更新する。
/// </summary>
void Camera::SetDistanceY(float dist)
{
	m_CameraDistanceY = dist;
	RefreshPosition();

}

void Camera::RefreshPosition()
{
	m_Position = m_Target + Vector3(0.0f, m_CameraDistanceY, -0.1f);
}	
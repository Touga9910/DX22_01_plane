#include "PlayerBall.h"
#include "GameBattleCommands.h"
#include "GameBattleEvents.h"
#include "GameBattleView.h"
#include "GameRuntime.h"
#include "GameShotPrediction.h"
#include "Camera.h"
#include "Application.h"
#include "input.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <chrono>
#include <cmath>

using namespace DirectX::SimpleMath;

// Mouse aiming and shot preparation; physics state remains in PlayerBall.cpp.
void PlayerBall::UpdateAim()
{
	BattleState battleState = GameBattleView::State();

	if (battleState != BattleState::AimingDirection &&
		battleState != BattleState::AimingPower)
	{
		return;
	}

	const bool imguiWantsMouse =
		ImGui::GetCurrentContext() != nullptr &&
		ImGui::GetIO().WantCaptureMouse;

	RECT client{};
	GetClientRect(Application::GetWindow(), &client);
	const auto mouse = Input::GetMousePosition();
	const bool mouseInGame = GetForegroundWindow() == Application::GetWindow() &&
		mouse.x >= 0 && mouse.y >= 0 && mouse.x < client.right && mouse.y < client.bottom;
	const bool leftPressed =
		Input::GetKeyTrigger(VK_LBUTTON) && !imguiWantsMouse && mouseInGame;
	const bool leftReleased =
		Input::GetKeyRelease(VK_LBUTTON);
	const bool rightPressed =
		Input::GetKeyTrigger(VK_RBUTTON) && (!imguiWantsMouse || m_IsPowerDragging);

	if (m_IsPowerDragging)
	{
		UpdateShotPowerFromMouseDrag();

		if (rightPressed || !mouseInGame || imguiWantsMouse)
		{
			CancelMousePowerDrag();
		}
		else if (leftReleased)
		{
			UpdateShotPowerFromMouseDrag();
			FireMouseShot();
			return;
		}
	}
	else
	{
		if (battleState != BattleState::AimingDirection)
		{
			GameBattleCommands::BeginAimDirection();
		}

		UpdateAimDirectionFromMouse();

		if (leftPressed)
		{
			//UpdateAimDirectionFromMouse();
			BeginMousePowerDrag();
		}
		else if (rightPressed)
		{
			CancelMousePowerDrag();
		}
	}

	const auto worldKeyStarted = std::chrono::steady_clock::now();
	const std::uint64_t currentPreviewWorldKey =
		GameShotPrediction::PreviewWorldKey();
	GameRuntime::RecordTrajectoryPreviewWorldKey(
		std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now() - worldKeyStarted).count());
	bool previewChanged =
		fabs(m_AimAngle - m_LastPreviewAimAngle) > 0.001f ||
		fabs(m_ShotPower - m_LastPreviewShotPower) > 0.001f ||
		(m_Ball->GetPosition() - m_LastPreviewPosition).LengthSquared() > 0.01f ||
		m_LastPreviewWorldKey != currentPreviewWorldKey;

	constexpr int kPreviewRefreshCooldownFrames = 1;
	const bool immediateRefresh =
		m_PreTrajectoryDirty || m_PrePositions.empty();
	const bool delayedRefresh =
		previewChanged && m_PreviewRefreshFramesRemaining <= 0;
	if (immediateRefresh || delayedRefresh)
	{
		GameRuntime::RecordTrajectoryPreviewRefresh();
		GeneratePreTrajectory(
			GetShotVector(), currentPreviewWorldKey);

		m_LastPreviewAimAngle = m_AimAngle;
		m_LastPreviewShotPower = m_ShotPower;
		m_LastPreviewPosition = m_Ball->GetPosition();
		m_PreTrajectoryDirty = false;
		m_PreviewRefreshFramesRemaining =
			kPreviewRefreshCooldownFrames;
	}
	else if (previewChanged)
	{
		--m_PreviewRefreshFramesRemaining;
	}
	else
	{
		m_PreviewRefreshFramesRemaining = 0;
		GameRuntime::RecordTrajectoryPreviewCacheReuse();
	}
}

bool PlayerBall::TryGetMouseAimPosition(Vector3& aimPosition) const
{
	// ウィンドウハンドルを取得する
	HWND hwnd = Application::GetWindow();
	if (hwnd == nullptr)
	{
		return false;
	}

	// ウィンドウ内の描画を取得する
	RECT clientRect{};
	if (!GetClientRect(hwnd, &clientRect))
	{
		return false;
	}

	// ウィンドウのクライアント領域の幅と高さを計算する
	float clientWidth = static_cast<float>(clientRect.right - clientRect.left);
	float clientHeight = static_cast<float>(clientRect.bottom - clientRect.top);
	if (clientWidth <= 0.0f || clientHeight <= 0.0f)
	{
		return false;
	}

	// ビューポートの計算（アスペクト比を維持するために黒帯を考慮）//
	float viewportX = 0.0f;
	float viewportY = 0.0f;
	float viewportWidth = clientWidth;
	float viewportHeight = clientHeight;

	// ゲーム側が考慮する画面比
	const float targetAspect = static_cast<float>(Application::GetWidth()) /
		static_cast<float>(Application::GetHeight());

	// 実際のウィンドウ比
	const float windowAspect = clientWidth / clientHeight;

	// アスペクト比を維持するために黒帯を考慮してビューポートを計算（黒帯でにマウスを向けても狙いがずれない）
	if (windowAspect > targetAspect)
	{
		viewportHeight = clientHeight;
		viewportWidth = clientHeight * targetAspect;
		viewportX = (clientWidth - viewportWidth) * 0.5f;
	}
	else
	{
		viewportWidth = clientWidth;
		viewportHeight = clientWidth / targetAspect;
		viewportY = (clientHeight - viewportHeight) * 0.5f;
	}

	// マウス位置をビューポート内に制限する（黒帯外のマウス位置は端に固定）
	DirectX::XMFLOAT2 mousePos = Input::GetMousePosition();    // まだ画面上の2D座標

	// マウス座標をビューポート内に制限する（画面外に行ったら、一番近い画面端に補正）
	const float viewportRight = viewportX + viewportWidth;
	const float viewportBottom = viewportY + viewportHeight;
	float mouseX = mousePos.x < viewportX ? viewportX : (mousePos.x > viewportRight ? viewportRight : mousePos.x);
	float mouseY = mousePos.y < viewportY ? viewportY : (mousePos.y > viewportBottom ? viewportBottom : mousePos.y);

	// プロジェクション行列を取得（視野角、アスペクト比、ニア・ファー平面の設定）
	constexpr float fieldOfView = DirectX::XMConvertToRadians(45.0f);
	constexpr float nearPlane = 1.0f;
	constexpr float farPlane = 1000.0f;
	DirectX::XMMATRIX projection = DirectX::XMMatrixPerspectiveFovLH(
		fieldOfView,
		targetAspect,
		nearPlane,
		farPlane);

	// ビュー行列を取得（カメラの位置と向き）
	Matrix viewMatrix = Camera::GetInstance().GetViewMatrix();
	DirectX::XMMATRIX view = viewMatrix;

	// マウス位置をワールド座標に変換するために、ニア平面とファー平面の2点を取得する //

	// ニア平面の座標を取得
	DirectX::XMVECTOR nearPointVector = DirectX::XMVector3Unproject(
		DirectX::XMVectorSet(mouseX, mouseY, 0.0f, 1.0f),
		viewportX,
		viewportY,
		viewportWidth,
		viewportHeight,
		0.0f,
		1.0f,
		projection,
		view,
		DirectX::XMMatrixIdentity());

	// ファー平面の座標を取得
	DirectX::XMVECTOR farPointVector = DirectX::XMVector3Unproject(
		DirectX::XMVectorSet(mouseX, mouseY, 1.0f, 1.0f),
		viewportX,
		viewportY,
		viewportWidth,
		viewportHeight,
		0.0f,
		1.0f,
		projection,
		view,
		DirectX::XMMatrixIdentity());

	// XMVECTORを、DirectX::SimpleMath::Vector3に変換する
	DirectX::XMFLOAT3 nearFloat{};
	DirectX::XMFLOAT3 farFloat{};
	DirectX::XMStoreFloat3(&nearFloat, nearPointVector);
	DirectX::XMStoreFloat3(&farFloat, farPointVector);

	Vector3 nearPoint(nearFloat.x, nearFloat.y, nearFloat.z);
	Vector3 farPoint(farFloat.x, farFloat.y, farFloat.z);

	// ニア平面とファー平面の2点から、マウス位置を通るレイを作る
	Vector3 ray = farPoint - nearPoint;

	// レイのy成分がほぼ0の場合は、床面との交点が計算できないので失敗とする
	if (std::fabs(ray.y) <= 0.0001f)
	{
		return false;
	}

	// レイのパラメータtを計算して、床面（y=ボールの高さ）との交点を求める
	const Vector3 ballPosition = m_Ball->GetPosition();
	float t = (ballPosition.y - nearPoint.y) / ray.y;

	// tが負の場合は、レイが床面の下方向を向いているので失敗とする
	if (t < 0.0f)
	{
		return false;
	}

	// 床面の交点を計算して、aimPositionに格納する
	aimPosition = nearPoint + ray * t;
	aimPosition.y = ballPosition.y;
	return true;
}

void PlayerBall::UpdateAimDirectionFromMouse()
{
	Vector3 aimPosition;                         // マウスが指している床上の座標
	if (!TryGetMouseAimPosition(aimPosition))
	{
		return;
	}

	Vector3 aimVector = aimPosition - m_Ball->GetPosition();
	aimVector.y = 0.0f;                           // 水平方向だけで角度を決める
	if (aimVector.LengthSquared() <= 0.0001f)
	{
		return;
	}

	m_AimAngle = static_cast<float>(std::atan2(aimVector.x, aimVector.z));
}

void PlayerBall::BeginMousePowerDrag()
{
	m_IsPowerDragging = true;                                             // パワードラッグ中にする
	m_LockedAimAngle = m_AimAngle;                                        // クリック時点の角度を固定する
	m_LockedShotDirection = Vector3(sin(m_LockedAimAngle), 0.0f, cos(m_LockedAimAngle));
	m_LockedShotDirection.Normalize();                                    // ショット方向として使えるよう正規化する
	m_PowerDragStartMousePos = Input::GetMousePosition();                 // パワー計算の基準位置を保存する
	m_ShotPower = m_MinShotPower;                                         // ドラッグ開始時は最小パワーにする
	m_PreTrajectoryDirty = true;                                          // 予測線を再計算対象にする
	GameBattleCommands::BeginPowerSelection();
}

void PlayerBall::UpdateShotPowerFromMouseDrag()
{
	m_AimAngle = m_LockedAimAngle;                                        // ドラッグ中は方向を固定する

	DirectX::XMFLOAT2 mousePos = Input::GetMousePosition();               // 現在のマウス座標を取得する

	const float dx = mousePos.x - m_PowerDragStartMousePos.x;             // ドラッグ開始位置からのX差分
	const float dy = mousePos.y - m_PowerDragStartMousePos.y;             // ドラッグ開始位置からのY差分
	const float dragDistance = std::sqrt(dx * dx + dy * dy);              // ドラッグ距離を計算する

	const float powerRatio =
		std::clamp(dragDistance / m_PixelsForMaxShotPower, 0.0f, 1.0f);    // 距離を0.0〜1.0の割合にする

	const float rawPower =
		m_MinShotPower + (m_MaxShotPower - m_MinShotPower) * powerRatio;   // 割合からパワー値を計算する

	const float steppedPower =
		std::floor(rawPower / m_PowerPreviewStep + 0.5f) * m_PowerPreviewStep; // 予測線の揺れを抑えるため丸める

	m_ShotPower = std::clamp(steppedPower, m_MinShotPower, m_MaxShotPower);
}

void PlayerBall::CancelMousePowerDrag()
{
	m_IsPowerDragging = false;                                      // パワードラッグを終了する
	m_PreTrajectoryDirty = true;                                    // 方向合わせに戻るため予測線を更新対象にする
	GameBattleCommands::CancelShot();				// 方向合わせ状態へ戻す
}

void PlayerBall::CancelShotPreparationInput()
{
	CancelMousePowerDrag();
}

void PlayerBall::FireMouseShot()
{
	// Legacy mouse shots move once before friction starts on the next tick.
	m_SkipSimulationOnNextFixedUpdate = true;
	m_AimAngle = m_LockedAimAngle;                                  // 固定していた角度を現在角度に反映する
	Shot(GetShotVector());                                          // 固定方向と現在パワーで速度を設定する
	m_IsPowerDragging = false;                                      // パワードラッグを終了する
	m_State = State::Simulation;                                    // 物理演算中に切り替える
	m_PrePositions.clear();                                         // ショット開始後は予測線を消す
	m_PreTrajectoryDirty = true;                                    // 次回停止後に予測線を再計算できるようにする
	m_StopCount = 0;                                                // 停止判定カウントをリセットする
	GameShotPrediction::WriteVerification(
		*this, GetVelocity(), m_SkipSimulationOnNextFixedUpdate);
	GameBattleEvents::PlayerShotFired(this);
}


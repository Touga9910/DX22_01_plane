#include "input.h"
#include "Application.h"

Input* Input::m_Instance = {};

/// <summary>
/// 入力管理のインスタンスを生成し、振動の残り時間とマウス座標の初期値を設定する。
/// </summary>
void Input::Create()
{
	if (m_Instance)return;
	m_Instance = new Input;

	m_Instance->VibrationTime = 0;

	GetCursorPos(&m_Instance->mousePos);
	ScreenToClient(Application::GetWindow(), &m_Instance->mousePos);
	m_Instance->mousePos_old = m_Instance->mousePos;
}

/// <summary>
/// 前フレームの入力を保存し、キーボード、マウス、コントローラーの最新状態と振動の残り時間を更新する。
/// </summary>
void Input::Update()
{
	for (int i = 0; i < 256; i++) { m_Instance->keyState_old[i] = m_Instance->keyState[i]; }
	m_Instance->controllerState_old = m_Instance->controllerState;
	m_Instance->mousePos_old = m_Instance->mousePos;

	BOOL hr = GetKeyboardState(m_Instance->keyState);

	GetCursorPos(&m_Instance->mousePos);
	ScreenToClient(Application::GetWindow(), &m_Instance->mousePos);

	XInputGetState(0, &(m_Instance->controllerState));

	if (m_Instance->VibrationTime > 0) {
		m_Instance->VibrationTime--;
		if (m_Instance->VibrationTime == 0) {
			XINPUT_VIBRATION vibration;
			ZeroMemory(&vibration, sizeof(XINPUT_VIBRATION));
			vibration.wLeftMotorSpeed = 0;
			vibration.wRightMotorSpeed = 0;
			XInputSetState(0, &vibration);
		}
	}
}

/// <summary>
/// コントローラーの振動を止め、入力管理のインスタンスを解放する。
/// </summary>
void Input::Release()
{
	//振動を終了させる
	XINPUT_VIBRATION vibration;
	ZeroMemory(&vibration, sizeof(XINPUT_VIBRATION));
	vibration.wLeftMotorSpeed = 0;
	vibration.wRightMotorSpeed = 0;
	XInputSetState(0, &vibration);

	//解放
	if (m_Instance)
	{
		delete m_Instance;
		m_Instance = NULL;
	}
}

/// <summary>
/// 指定したキーが現在押されているかを返す。
/// </summary>
bool Input::GetKeyPress(int key)
{
	return m_Instance->keyState[key] & 0x80;
}
/// <summary>
/// 指定したキーがこのフレームで押されたかを返す。
/// </summary>
bool Input::GetKeyTrigger(int key)
{
	return (m_Instance->keyState[key] & 0x80) && !(m_Instance->keyState_old[key] & 0x80);
}
/// <summary>
/// 指定したキーがこのフレームで離されたかを返す。
/// </summary>
bool Input::GetKeyRelease(int key)
{
	return !(m_Instance->keyState[key] & 0x80) && (m_Instance->keyState_old[key] & 0x80);
}

/// <summary>
/// 左アナログスティックの横方向と縦方向の入力値を正規化して返す。
/// </summary>
DirectX::XMFLOAT2 Input::GetLeftAnalogStick(void)
{
	SHORT x = m_Instance->controllerState.Gamepad.sThumbLX; // -32768～32767
	SHORT y = m_Instance->controllerState.Gamepad.sThumbLY; // -32768～32767

	DirectX::XMFLOAT2 res;
	res.x = x / 32767.0f; //-1～1
	res.y = y / 32767.0f; //-1～1
	return res;
}
/// <summary>
/// 右アナログスティックの横方向と縦方向の入力値を正規化して返す。
/// </summary>
DirectX::XMFLOAT2 Input::GetRightAnalogStick(void)
{
	SHORT x = m_Instance->controllerState.Gamepad.sThumbRX; // -32768～32767
	SHORT y = m_Instance->controllerState.Gamepad.sThumbRY; // -32768～32767

	DirectX::XMFLOAT2 res;
	res.x = x / 32767.0f; //-1～1
	res.y = y / 32767.0f; //-1～1
	return res;
}

/// <summary>
/// 左トリガーの入力値を0から1の範囲に変換して返す。
/// </summary>
float Input::GetLeftTrigger(void)
{
	BYTE t = m_Instance->controllerState.Gamepad.bLeftTrigger; // 0～255
	return t / 255.0f;
}
/// <summary>
/// 右トリガーの入力値を0から1の範囲に変換して返す。
/// </summary>
float Input::GetRightTrigger(void)
{
	BYTE t = m_Instance->controllerState.Gamepad.bRightTrigger; // 0～255
	return t / 255.0f;
}

/// <summary>
/// 指定したボタンが現在押されているかを返す。
/// </summary>
bool Input::GetButtonPress(WORD btn)
{
	return (m_Instance->controllerState.Gamepad.wButtons & btn) != 0;
}
/// <summary>
/// 指定したボタンがこのフレームで押されたかを返す。
/// </summary>
bool Input::GetButtonTrigger(WORD btn)
{
	return (m_Instance->controllerState.Gamepad.wButtons & btn) != 0 && (m_Instance->controllerState_old.Gamepad.wButtons & btn) == 0;
}
/// <summary>
/// 指定したボタンがこのフレームで離されたかを返す。
/// </summary>
bool Input::GetButtonRelease(WORD btn)
{
	return (m_Instance->controllerState.Gamepad.wButtons & btn) == 0 && (m_Instance->controllerState_old.Gamepad.wButtons & btn) != 0;
}

/// <summary>
/// コントローラーの両方のモーターを指定した強さで動かし、持続フレーム数を設定する。
/// </summary>
void Input::SetVibration(int frame, float powor)
{
	// XINPUT_VIBRATION構造体のインスタンスを作成
	XINPUT_VIBRATION vibration;
	ZeroMemory(&vibration, sizeof(XINPUT_VIBRATION));

	// モーターの強度を設定（0～65535）
	vibration.wLeftMotorSpeed = (WORD)(powor * 65535.0f);
	vibration.wRightMotorSpeed = (WORD)(powor * 65535.0f);
	XInputSetState(0, &vibration);

	//振動継続時間を代入
	m_Instance->VibrationTime = frame;
}

// マウスの現在位置を取得
/// <summary>
/// 現在のマウス位置を2次元座標で返す。
/// </summary>
DirectX::XMFLOAT2 Input::GetMousePosition()
{
	return DirectX::XMFLOAT2((float)m_Instance->mousePos.x, (float)m_Instance->mousePos.y);
}

// 1フレームでのマウスの移動量を取得
/// <summary>
/// 現在と前回のマウス位置の差を返す。
/// </summary>
DirectX::XMFLOAT2 Input::GetMouseMove()
{
	float dx = (float)(m_Instance->mousePos.x - m_Instance->mousePos_old.x);
	float dy = (float)(m_Instance->mousePos.y - m_Instance->mousePos_old.y);

	return DirectX::XMFLOAT2(dx, dy);
}
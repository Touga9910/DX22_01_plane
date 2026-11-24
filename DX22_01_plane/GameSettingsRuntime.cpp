#include "GameSettingsRuntime.h"

/// <summary>
/// 保存済みのゲーム設定を読み込み、画面表示に反映する。
/// </summary>
void GameSettingsRuntime::Initialize()
{
	m_Manager.Load();
	m_Manager.ApplyDisplaySettings();
}

/// <summary>
/// 現在の画面表示の設定をウィンドウに適用する。
/// </summary>
void GameSettingsRuntime::ApplyDisplaySettings()
{
	m_Manager.ApplyDisplaySettings();
}

/// <summary>
/// フルスクリーンの設定を切り替え、画面へ反映して保存する。
/// </summary>
void GameSettingsRuntime::ToggleFullscreen()
{
	GameSettings& settings = m_Manager.Edit();
	settings.fullscreen = !settings.fullscreen;
	m_Manager.MarkDirty();
	m_Manager.ApplyDisplaySettings();
	m_Manager.SaveIfDirty();
}

/// <summary>
/// 変更されたゲーム設定を必要に応じて保存する。
/// </summary>
void GameSettingsRuntime::SaveIfDirty()
{
	m_Manager.SaveIfDirty();
}

/// <summary>
/// 現在のゲーム設定を参照として返す。
/// </summary>
const GameSettings& GameSettingsRuntime::Current() const
{
	return m_Manager.Get();
}

/// <summary>
/// ゲーム設定を差し替えて遷移時間を正規化し、振動を無効化したかを返す。
/// </summary>
bool GameSettingsRuntime::Replace(const GameSettings& settings)
{
	const bool vibrationTurnedOff =
		m_Manager.Get().vibrationEnabled && !settings.vibrationEnabled;
	m_Manager.Edit() = settings;
	m_Manager.Edit().sceneTransition.Normalize();
	m_Manager.MarkDirty();
	return vibrationTurnedOff;
}

/// <summary>
/// コントローラーの振動設定が有効かを返す。
/// </summary>
bool GameSettingsRuntime::IsVibrationEnabled() const
{
	return m_Manager.Get().vibrationEnabled;
}

/// <summary>
/// 画面フラッシュの設定が有効かを返す。
/// </summary>
bool GameSettingsRuntime::IsScreenFlashEnabled() const
{
	return m_Manager.Get().screenFlashEnabled;
}

/// <summary>
/// カメラの揺れの設定が有効かを返す。
/// </summary>
bool GameSettingsRuntime::IsCameraShakeEnabled() const
{
	return m_Manager.Get().cameraShakeEnabled;
}

/// <summary>
/// 衝突時のエフェクトの設定が有効かを返す。
/// </summary>
bool GameSettingsRuntime::IsCollisionImpactEffectsEnabled() const
{
	return m_Manager.Get().collisionImpactEffectsEnabled;
}

/// <summary>
/// 強い連鎖に伴うエフェクトの設定が有効かを返す。
/// </summary>
bool GameSettingsRuntime::IsHeavyChainEffectsEnabled() const
{
	return m_Manager.Get().heavyChainEffectsEnabled;
}

/// <summary>
/// 敵の被弾時の発光設定が有効かを返す。
/// </summary>
bool GameSettingsRuntime::IsEnemyHitFlashEnabled() const
{
	return m_Manager.Get().enemyHitFlashEnabled;
}

/// <summary>
/// 敵撃破時の視覚効果の設定が有効かを返す。
/// </summary>
bool GameSettingsRuntime::IsDefeatVfxEnabled() const
{
	return m_Manager.Get().defeatVfxEnabled;
}

/// <summary>
/// 複数の敵を撃破した際のエフェクトの設定が有効かを返す。
/// </summary>
bool GameSettingsRuntime::IsMultiDefeatEffectsEnabled() const
{
	return m_Manager.Get().multiDefeatEffectsEnabled;
}

/// <summary>
/// フレーム時間の計測設定が有効かを返す。
/// </summary>
bool GameSettingsRuntime::IsFrameTimeMeasurementEnabled() const
{
	return m_Manager.Get().frameTimeMeasurementEnabled;
}

/// <summary>
/// 現在の装飾リングの表示量の設定を返す。
/// </summary>
int GameSettingsRuntime::GetDecorativeRingAmount() const
{
	return m_Manager.Get().decorativeRingAmount;
}

/// <summary>
/// 現在の敵HP表示の設定を返す。
/// </summary>
const EnemyHpDisplaySettings& GameSettingsRuntime::GetEnemyHpDisplaySettings() const
{
	return m_Manager.Get().enemyHpDisplay;
}

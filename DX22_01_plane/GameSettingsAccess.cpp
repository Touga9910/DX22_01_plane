#include "Game.h"
#include "GameSettingsRuntime.h"

#pragma execution_character_set("utf-8")

/// <summary>
/// 現在のシーン遷移に使う時間設定を返す。
/// </summary>
const SceneTransitionTiming& Game::GetSceneTransitionTiming() const
{
	return m_SettingsRuntime->Current().sceneTransition;
}

/// <summary>
/// コントローラーの振動が有効かを返す。
/// </summary>
bool Game::IsVibrationEnabled() const
{
	return m_SettingsRuntime->IsVibrationEnabled();
}

/// <summary>
/// 画面フラッシュが有効かを返す。
/// </summary>
bool Game::IsScreenFlashEnabled() const
{
	return m_SettingsRuntime->IsScreenFlashEnabled();
}

/// <summary>
/// カメラの揺れが有効かを返す。
/// </summary>
bool Game::IsCameraShakeEnabled() const
{
	return m_SettingsRuntime->IsCameraShakeEnabled();
}

/// <summary>
/// 衝突時のエフェクトが有効かを返す。
/// </summary>
bool Game::IsCollisionImpactEffectsEnabled() const
{
	return m_SettingsRuntime->IsCollisionImpactEffectsEnabled();
}

/// <summary>
/// 強い連鎖に伴うエフェクトが有効かを返す。
/// </summary>
bool Game::IsHeavyChainEffectsEnabled() const
{
	return m_SettingsRuntime->IsHeavyChainEffectsEnabled();
}

/// <summary>
/// 敵の被弾時の発光が有効かを返す。
/// </summary>
bool Game::IsEnemyHitFlashEnabled() const
{
	return m_SettingsRuntime->IsEnemyHitFlashEnabled();
}

/// <summary>
/// 敵撃破時の視覚効果が有効かを返す。
/// </summary>
bool Game::IsDefeatVfxEnabled() const
{
	return m_SettingsRuntime->IsDefeatVfxEnabled();
}

/// <summary>
/// 複数の敵を撃破した際のエフェクトが有効かを返す。
/// </summary>
bool Game::IsMultiDefeatEffectsEnabled() const
{
	return m_SettingsRuntime->IsMultiDefeatEffectsEnabled();
}

/// <summary>
/// フレーム時間の計測が有効かを返す。
/// </summary>
bool Game::IsFrameTimeMeasurementEnabled() const
{
	return m_SettingsRuntime->IsFrameTimeMeasurementEnabled();
}

/// <summary>
/// 装飾リングの表示量の設定を返す。
/// </summary>
int Game::GetDecorativeRingAmount() const
{
	return m_SettingsRuntime->GetDecorativeRingAmount();
}

/// <summary>
/// 敵のHP表示に使う設定を返す。
/// </summary>
const EnemyHpDisplaySettings& Game::GetEnemyHpDisplaySettings() const
{
	return m_SettingsRuntime->GetEnemyHpDisplaySettings();
}

/// <summary>
/// 設定済みの効果音音量を返す。設定が未初期化の場合は0を返す。
/// </summary>
float Game::GetSeVolume() const
{
	return m_SettingsRuntime != nullptr
		? m_SettingsRuntime->Current().seVolume
		: 0.0f;
}

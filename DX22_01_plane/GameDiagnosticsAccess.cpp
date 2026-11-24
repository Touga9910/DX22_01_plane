#include "Game.h"
#include "GamePresentation.h"
#include "AudioFeedback.h"
#include "GameRuntimeState.h"

#pragma execution_character_set("utf-8")

/// <summary>
/// 衝突の視覚演出に関する診断情報を取得し、演出が未初期化なら既定値を返す。
/// </summary>
const CollisionVfxDiagnostics& Game::GetCollisionVfxDiagnostics() const
{
	static const CollisionVfxDiagnostics empty{};
	if (m_GamePresentation == nullptr) return empty;
	return m_GamePresentation->GetCollisionVfxDiagnostics();
}

/// <summary>
/// 戦闘のリアクション演出の診断情報を取得し、演出が未初期化なら既定値を返す。
/// </summary>
const CombatReactionDiagnostics& Game::GetCombatReactionDiagnostics() const
{
	static const CombatReactionDiagnostics empty{};
	if (m_GamePresentation == nullptr) return empty;
	return m_GamePresentation->GetCombatReactionDiagnostics();
}

/// <summary>
/// 大規模な衝突連鎖の演出を記録した件数を取得する。
/// </summary>
int Game::GetHeavyPhysicsChainPresentationCount() const
{
	if (m_GamePresentation == nullptr) return 0;
	return m_GamePresentation->GetHeavyPhysicsChainCount();
}

/// <summary>
/// 戦闘音声の診断情報を取得し、音声処理が未初期化なら既定値を返す。
/// </summary>
const AudioFeedbackDiagnostics& Game::GetAudioFeedbackDiagnostics() const
{
	static const AudioFeedbackDiagnostics empty{};
	if (m_AudioFeedback == nullptr) return empty;
	return m_AudioFeedback->Diagnostics();
}

/// <summary>
/// フレーム時間の計測結果を取得する。
/// </summary>
const FrameTimingSnapshot& Game::GetFrameTimingSnapshot() const
{
	return m_RuntimeState->CaptureFrameTiming();
}

/// <summary>
/// 照準処理に関する性能計測の結果を取得する。
/// </summary>
AimingPerformanceSnapshot Game::GetAimingPerformanceSnapshot() const
{
	return m_RuntimeState->CaptureAimingPerformance();
}

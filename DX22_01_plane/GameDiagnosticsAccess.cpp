#include "Game.h"
#include "GamePresentation.h"
#include "AudioFeedback.h"
#include "GameRuntimeState.h"

#pragma execution_character_set("utf-8")

const CollisionVfxDiagnostics& Game::GetCollisionVfxDiagnostics() const
{
	static const CollisionVfxDiagnostics empty{};
	if (m_GamePresentation == nullptr) return empty;
	return m_GamePresentation->GetCollisionVfxDiagnostics();
}

const CombatReactionDiagnostics& Game::GetCombatReactionDiagnostics() const
{
	static const CombatReactionDiagnostics empty{};
	if (m_GamePresentation == nullptr) return empty;
	return m_GamePresentation->GetCombatReactionDiagnostics();
}

int Game::GetHeavyPhysicsChainPresentationCount() const
{
	if (m_GamePresentation == nullptr) return 0;
	return m_GamePresentation->GetHeavyPhysicsChainCount();
}

const AudioFeedbackDiagnostics& Game::GetAudioFeedbackDiagnostics() const
{
	static const AudioFeedbackDiagnostics empty{};
	if (m_AudioFeedback == nullptr) return empty;
	return m_AudioFeedback->Diagnostics();
}

const FrameTimingSnapshot& Game::GetFrameTimingSnapshot() const
{
	return m_RuntimeState->CaptureFrameTiming();
}

AimingPerformanceSnapshot Game::GetAimingPerformanceSnapshot() const
{
	return m_RuntimeState->CaptureAimingPerformance();
}

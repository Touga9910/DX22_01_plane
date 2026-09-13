#pragma once

#include <SimpleMath.h>

#include <array>
#include <string>
#include <vector>

class Game;

// プレイヤー向けのチュートリアル、戦闘フィードバック、バランス可視化を扱う。
// 自動ランの決定性を保つため、ゲームルールから分離する。
class GamePresentation final
{
public:
	void Initialize(Game& game);
	void Update(Game& game);
	void Draw(Game& game);
	void DrawPause(Game& game);
	void DrawBallSelection(Game& game);
	void DrawClearReward(Game& game);

	void OnBattleStarted(Game& game);
	void OnShotFired();
	void OnCombatFeedback(
		const DirectX::SimpleMath::Vector3& worldPosition,
		int damage,
		bool defeated,
		bool enemyEnemyCollision);
	void OnChainImpact(
		const DirectX::SimpleMath::Vector3& worldPosition,
		float radius,
		int hitCount);
	void OnPocketFeedback(
		const DirectX::SimpleMath::Vector3& worldPosition,
		bool playerPocket,
		bool finisher,
		int damage);
	void OnPlayerDamage(int damage, const std::string& source);

private:
	enum class TutorialStep
	{
		Welcome,
		ChooseAndAim,
		SetPower,
		WatchShot,
		ReadResult,
		Complete,
	};

	struct FloatingFeedback
	{
		DirectX::SimpleMath::Vector3 worldPosition{};
		std::string text;
		float lifetime = 1.0f;
		float totalLifetime = 1.0f;
		unsigned int color = 0xffffffffu;
		bool drawRing = false;
		bool playerHudMessage = false;
	};

	struct ChainRangeFeedback
	{
		DirectX::SimpleMath::Vector3 worldPosition{};
		float radius = 0.0f;
		float lifetime = 1.0f;
		float totalLifetime = 1.0f;
		int hitCount = 0;
	};

	struct DashboardMetric
	{
		std::string id;
		std::string label;
		float value = 0.0f;
		float targetMin = 0.0f;
		float targetMax = 1.0f;
		bool percentage = false;
		bool inRange = false;
	};

	struct StageSummary
	{
		std::string id;
		std::string type;
		int sampleCount = 0;
		float clearRate = 0.0f;
		float medianShots = 0.0f;
		float remainingHpRate = 0.0f;
		float balanceScore = 0.0f;
		std::string judgement;
	};

	struct DashboardData
	{
		bool loaded = false;
		std::string error;
		std::string generatedAt;
		std::string warning;
		std::string judgement;
		int analyzedRuns = 0;
		int balanceSamples = 0;
		float balanceScore = 0.0f;
		float heavyShotShare = 0.0f;
		float standardShotShare = 0.0f;
		float tacticalPocketRate = 0.0f;
		float averageMoneyPerRun = 0.0f;
		std::vector<DashboardMetric> metrics;
		std::vector<StageSummary> stages;
	};

	void StartTutorial(bool replay);
	void CompleteTutorial();
	void SaveTutorialProgress() const;
	void DrawTutorial(Game& game);
	void DrawFeedback(Game& game);
	void DrawEnemyStatusEffects(Game& game);
	void DrawDashboard();
	void DrawBattleHud(Game& game);
	void DrawDeckList(Game& game);
	void DrawRelicList(Game& game);
	void LoadDashboard();
	bool IsBattleScene(const Game& game) const;

	enum class DeckListView
	{
		All,
		DrawPile,
		DiscardPile,
	};

	bool m_TutorialCompleted = false;
	bool m_TutorialActive = false;
	bool m_TutorialReplay = false;
	TutorialStep m_TutorialStep = TutorialStep::Welcome;
	int m_PreviousBattleState = -1;
	bool m_DashboardOpen = false;
	float m_ImpactFlashLifetime = 0.0f;
	float m_DamageFlashLifetime = 0.0f;
	float m_HitSummaryLifetime = 0.0f;
	float m_CameraShakeLifetime = 0.0f;
	float m_CameraShakeStrength = 0.0f;
	float m_CameraShakePhase = 0.0f;
	int m_CurrentShotHitCount = 0;
	int m_CurrentShotDamage = 0;
	std::array<float, 4> m_BallCardExpansion{};
	bool m_DeckListOpen = false;
	bool m_RelicListOpen = false;
	DeckListView m_DeckListView = DeckListView::All;
	std::vector<FloatingFeedback> m_Feedback;
	std::vector<ChainRangeFeedback> m_ChainRanges;
	DashboardData m_Dashboard;
};

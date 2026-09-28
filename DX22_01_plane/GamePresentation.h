#pragma once

#include "SettingsManager.h"

#include <SimpleMath.h>

#include <array>
#include <string>
#include <vector>

class Game;
struct PlayerBallData;
struct RelicDefinition;

struct PausePresentationModel
{
	GameSettings settings{};
	bool debugMode = false;
	bool confirmReturnToTitle = false;
	bool battleScene = false;
	bool clearRewardActive = false;
};

struct PausePresentationIntent
{
	GameSettings settings{};
	bool settingsChanged = false;
	bool resume = false;
	bool applyDisplay = false;
	bool requestDebugExit = false;
	bool requestReturnConfirmation = false;
	bool saveAndReturnToTitle = false;
	bool cancelReturnToTitle = false;
};

struct BallSelectionPresentationModel
{
	std::vector<const PlayerBallData*> offers;
	std::vector<int> effectiveAttacks;
	int selectedOfferIndex = 0;
	int selectedHoldIndex = -1;
};

struct BallSelectionPresentationIntent
{
	int selectedOfferIndex = 0;
	int selectedHoldIndex = -1;
	bool selectionChanged = false;
};

struct ClearRewardBallView
{
	const PlayerBallData* ball = nullptr;
	int upgradeCost = -1;
};

struct ClearRewardPresentationModel
{
	int stageRewardMoney = 0;
	int playerMoney = 0;
	int playerCurrentHp = 0;
	int playerMaxHp = 0;
	bool midBossRelicSelectionActive = false;
	bool clearRewardChosen = false;
	int selectedRelicOfferIndex = 0;
	int selectedRewardIndex = 0;
	int selectedRewardBallIndex = 0;
	int extraMoneyAmount = 0;
	std::string rewardMessage;
	std::vector<const RelicDefinition*> relicOffers;
	std::vector<const PlayerBallData*> newBallOffers;
	std::vector<ClearRewardBallView> upgradeTargets;
};

struct ClearRewardPresentationIntent
{
	int selectedRelicOfferIndex = 0;
	int selectedRewardIndex = 0;
	int selectedRewardBallIndex = 0;
	bool confirm = false;
};

// プレイヤー向けのチュートリアル、戦闘フィードバック、バランス可視化を扱う。
// 自動ランの決定性を保つため、ゲームルールから分離する。
class GamePresentation final
{
public:
	void Initialize(Game& game);
	void Update(Game& game);
	void Draw(Game& game);
	PausePresentationIntent DrawPause(const PausePresentationModel& model);
	BallSelectionPresentationIntent DrawBallSelection(
		const BallSelectionPresentationModel& model);
	ClearRewardPresentationIntent DrawClearReward(
		const ClearRewardPresentationModel& model);

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
	void DrawPierceTraces(Game& game);
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
	int m_BallDetailOfferIndex = -1;
	bool m_DeckListOpen = false;
	bool m_RelicListOpen = false;
	DeckListView m_DeckListView = DeckListView::All;
	std::vector<FloatingFeedback> m_Feedback;
	std::vector<ChainRangeFeedback> m_ChainRanges;
	DashboardData m_Dashboard;
};

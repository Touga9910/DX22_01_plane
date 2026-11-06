#pragma once

#include "CollisionVfxSystem.h"
#include "CombatPresentationRules.h"

#include "SettingsManager.h"

#include <SimpleMath.h>

#include <array>
#include <string>
#include <vector>

class Game;
struct CollisionImpactEvent;
struct AxisResourceChangedEvent;
struct AxisAbilityResolvedEvent;
struct ShotResolvedEvent;
enum class ChargeInterruptReason;
struct PlayerBallData;
struct RelicDefinition;

struct CombatReactionDiagnostics
{
	int hitFlashRequests = 0;
	int hitFlashesTriggered = 0;
	int zeroDamageFlashesSuppressed = 0;
	int hitFlashesSkipped = 0;
	int activeHitFlashes = 0;
	int maximumActiveHitFlashes = 0;
	int defeatRequests = 0;
	int uniqueDefeats = 0;
	int duplicateDefeatsSuppressed = 0;
	int multiDefeatDisplays = 0;
};

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
	void Update(Game& game, float deltaSeconds);
	void Draw(Game& game);
	PausePresentationIntent DrawPause(const PausePresentationModel& model);
	BallSelectionPresentationIntent DrawBallSelection(
		const BallSelectionPresentationModel& model);
	ClearRewardPresentationIntent DrawClearReward(
		const ClearRewardPresentationModel& model);

	void OnBattleStarted(Game& game);
	void OnShotFired(int ballCategory);
	void OnCollisionImpact(const CollisionImpactEvent& event);
	const CollisionVfxDiagnostics& GetCollisionVfxDiagnostics() const noexcept
	{
		return m_CollisionVfx.GetDiagnostics();
	}
	const CombatReactionDiagnostics& GetCombatReactionDiagnostics() const noexcept
	{
		return m_CombatReactionDiagnostics;
	}
	int GetHeavyPhysicsChainCount() const noexcept
	{
		return m_HeavyPhysicsChainCount;
	}
	void OnCombatFeedback(
		const DirectX::SimpleMath::Vector3& worldPosition,
		int damage,
		bool defeated,
		bool enemyEnemyCollision,
		std::uintptr_t targetKey,
		const std::string& targetEnemyId);
	void OnChainImpact(
		const DirectX::SimpleMath::Vector3& worldPosition,
		float radius,
		int hitCount);
	void OnPocketFeedback(
		const DirectX::SimpleMath::Vector3& worldPosition,
		bool playerPocket,
		bool finisher,
		int damage,
		std::uintptr_t targetKey,
		const std::string& targetEnemyId);
	void OnPlayerDamage(int damage, const std::string& source);
	void OnChargeInterrupted(
		const DirectX::SimpleMath::Vector3& worldPosition,
		ChargeInterruptReason reason);
	void OnAxisResourceChanged(const AxisResourceChangedEvent& event);
	void OnAxisAbilityResolved(const AxisAbilityResolvedEvent& event);
	void OnShotResolved(const ShotResolvedEvent& event);

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
		bool enemyEnemyCollision = false;
		float fontScale = 1.0f;
		unsigned long long createdFrame = 0;
		float createdTimeSeconds = 0.0f;
		std::uintptr_t targetKey = 0;
		int resourceAxis = -1;
		int resourceOwner = 0;
		int resourceBefore = 0;
		int resourceAfter = 0;
		int resourceReason = 0;
		bool finisher = false;
	};

	struct ChainRangeFeedback
	{
		DirectX::SimpleMath::Vector3 worldPosition{};
		float radius = 0.0f;
		float lifetime = 1.0f;
		float totalLifetime = 1.0f;
		int hitCount = 0;
	};

	struct HitReaction
	{
		std::uintptr_t targetKey = 0;
		float lifetime = 0.0f;
		float totalLifetime = 0.0f;
		float intensity = 0.0f;
		bool active = false;
	};

	struct EnemyHpVisual
	{
		std::uintptr_t targetKey = 0;
		int currentHp = 0;
		int maximumHp = 1;
		float trailingHp = 0.0f;
		float damageHold = 0.0f;
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
	void BeginHitReaction(std::uintptr_t targetKey, int damage, bool defeated);
	bool BeginDefeatReaction(
		const DirectX::SimpleMath::Vector3& worldPosition,
		std::uintptr_t targetKey);
	bool HasTrackedDefeat(std::uintptr_t targetKey) const;
	void UpdateHitReactions(Game& game, float deltaSeconds);
	void UpdateEnemyHpVisuals(float deltaSeconds);
	void ClearHitReactions();

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
	int m_CurrentShotChargeInterruptions = 0;
	std::array<int, 4> m_CurrentShotResourceGained{};
	std::array<int, 4> m_CurrentShotResourceConsumed{};
	unsigned int m_CurrentShotFinisherMask = 0;
	std::array<float, 4> m_BallCardExpansion{};
	int m_BallDetailOfferIndex = -1;
	bool m_DeckListOpen = false;
	bool m_RelicListOpen = false;
	DeckListView m_DeckListView = DeckListView::All;
	std::vector<FloatingFeedback> m_Feedback;
	std::vector<EnemyHpVisual> m_EnemyHpVisuals;
	CollisionVfxSystem m_CollisionVfx;
	unsigned long long m_PresentationFrame = 0;
	float m_PresentationTimeSeconds = 0.0f;
	int m_CurrentShotBallCategory = 0;
	int m_HeavyPhysicsChainCount = 0;
	float m_HeavyChainDisplayLifetime = 0.0f;
	float m_HeavyChainDisplayTotalLifetime = 1.0f;
	bool m_HeavyChainMilestone = false;
	float m_ImpactFlashCooldown = 0.0f;
	float m_CameraShakeCooldown = 0.0f;
	std::vector<ChainRangeFeedback> m_ChainRanges;
	std::array<HitReaction, CombatPresentationRules::MaximumHitReactions>
		m_HitReactions{};
	std::array<std::uintptr_t, CombatPresentationRules::MaximumTrackedDefeats>
		m_DefeatKeys{};
	int m_TrackedDefeatCount = 0;
	int m_CurrentShotDefeatCount = 0;
	float m_MultiDefeatDisplayLifetime = 0.0f;
	float m_MultiDefeatDisplayTotalLifetime = 1.0f;
	CombatReactionDiagnostics m_CombatReactionDiagnostics{};
	DashboardData m_Dashboard;
};

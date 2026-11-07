#pragma once

#include <SimpleMath.h>

#include <array>
#include <cstdint>
#include <string>
#include <variant>

enum class CombatCollisionKind
{
	PlayerEnemy,
	EnemyEnemy,
	Wall,
};

enum class AxisResourceKind
{
	Heavy = 0,
	Pierce,
	Reflection,
	Anchor,
	Count,
};

enum class AxisResourceOwner
{
	Player,
	Enemy,
};

enum class AxisResourceChangeReason
{
	AbilityCost,
	Collision,
	TravelDistance,
	PierceHit,
	WallReflection,
	ShieldConversion,
	DefeatConversion,
	EndOfShot,
	Finisher,
};

// ルール側で確定した3軸能力の結果。描画方法ではなく、何がどこから
// どこへ起きたかだけをPresentationへ渡す。
enum class AxisAbilityAction
{
	PierceTraceCreated,
	PierceMinionLaunched,
	PierceMinionTravel,
	PierceFinisherHit,
	ReflectionWallBounce,
	ReflectionEmpoweredHit,
	ReflectionPinballActivated,
	ReflectionPinballCollision,
	AnchorApplied,
	AnchorRecovered,
	AnchorShieldGranted,
	AnchorShieldAbsorbed,
	AnchorFinisherArea,
	AnchorPlayerEmpowered,
};

// プレイヤーがボールを発射したことを通知するイベント
struct ShotFiredEvent
{
	std::string ballId;	// 発射したボールの定義ID
	int ballCategory = 0;
};

// One accepted collision episode. Enemy-enemy events are emitted from the
// same resolved contact used by ShotPhysicsTelemetry.
struct CollisionImpactEvent
{
	DirectX::SimpleMath::Vector3 worldPosition{};
	DirectX::SimpleMath::Vector3 firstTargetPosition{};
	DirectX::SimpleMath::Vector3 secondTargetPosition{};
	float relativeApproachSpeed = 0.0f;
	CombatCollisionKind collisionKind = CombatCollisionKind::PlayerEnemy;
	int damage = 0;
	bool defeated = false;
	std::uintptr_t firstTargetKey = 0;
	std::uintptr_t secondTargetKey = 0;
	std::string firstTargetEnemyId;
	std::string secondTargetEnemyId;
	int shotBallCategory = 0;
};

// An already accepted wall reflection. Presentation consumers must not run a
// second wall-contact test from this fact.
struct WallImpactEvent
{
	DirectX::SimpleMath::Vector3 worldPosition{};
	float relativeApproachSpeed = 0.0f;
	std::uintptr_t targetKey = 0;
};

// 敵へダメージが発生したことを通知するイベント
struct EnemyDamageEvent
{
	DirectX::SimpleMath::Vector3 worldPosition{};	// ダメージが発生したワールド座標
	int damage = 0;									// 与えたダメージ量
	bool defeated = false;							// このダメージで敵を撃破したか
	bool enemyEnemyCollision = false;				// 敵同士の衝突によるダメージか
	std::uintptr_t targetKey = 0;					// 表示対象を識別する同期イベント内キー
	std::string targetEnemyId;						// 診断用。ルール判定には使用しない
};

// 連鎖衝撃効果が発生したことを通知するイベント
struct ChainImpactEvent
{
	DirectX::SimpleMath::Vector3 worldPosition{};	// 連鎖衝撃の中心座標
	float radius = 0.0f;							// 効果半径
	int hitCount = 0;								// 効果によって命中した対象数
};

// ポケットに関するフィードバック表示へ渡すイベント
struct PocketFeedbackEvent
{
	DirectX::SimpleMath::Vector3 worldPosition{};	// ポケット処理が発生したワールド座標
	bool playerPocket = false;						// プレイヤーボールのポケット処理か
	bool finisher = false;							// フィニッシャー効果として処理されたか
	int damage = 0;									// ポケット処理で発生したダメージ量
	std::uintptr_t targetKey = 0;
	std::string targetEnemyId;
};

// プレイヤーへダメージが発生したことを通知するイベント
struct PlayerDamageEvent
{
	std::string source;		// ダメージ原因を表す文字列
	int damage = 0;			// 受けたダメージ量
	std::string sourceId;	// ダメージ元を識別するID
};

// 物理・ルール側で確定した軸リソースの変化だけをPresentationへ渡す。
// 描画方法や表示時間は一切含めない。
struct AxisResourceChangedEvent
{
	AxisResourceKind axis = AxisResourceKind::Heavy;
	AxisResourceOwner owner = AxisResourceOwner::Player;
	int valueBefore = 0;
	int valueAfter = 0;
	AxisResourceChangeReason reason = AxisResourceChangeReason::Collision;
	std::string abilityId;
	bool finisher = false;
	DirectX::SimpleMath::Vector3 worldPosition{};
	std::uintptr_t targetKey = 0;
};

struct AxisAbilityResolvedEvent
{
	AxisResourceKind axis = AxisResourceKind::Pierce;
	AxisAbilityAction action = AxisAbilityAction::PierceTraceCreated;
	DirectX::SimpleMath::Vector3 sourcePosition{};
	DirectX::SimpleMath::Vector3 targetPosition{};
	int amount = 0;
	int tier = 0;
	float radius = 0.0f;
	bool enhanced = false;
	bool finisher = false;
	std::uintptr_t sourceKey = 0;
	std::uintptr_t targetKey = 0;
	std::string abilityId;
};

struct ChargeInterruptedEvent
{
	DirectX::SimpleMath::Vector3 worldPosition{};
	int reason = 0;
	std::uintptr_t targetKey = 0;
	std::string targetEnemyId;
};

// A gameplay-side board-dependent enemy state changed. The event contains
// only the resolved fact; presentation never re-evaluates ranges or impacts.
struct EnemyAbilityStateChangedEvent
{
	DirectX::SimpleMath::Vector3 worldPosition{};
	std::uintptr_t targetKey = 0;
	std::string targetEnemyId;
	std::string abilityId;
	std::string reason;
	int previousStage = 0;
	int currentStage = 0;
	int previousAttack = 0;
	int currentAttack = 0;
	int nearbyLivingAllies = 0;
	float collisionSpeed = 0.0f;
};

// 物理停止とEndOfShot効果の確定後に1回だけ発行する表示用スナップショット。
struct ShotResolvedEvent
{
	int hitCount = 0;
	int totalDamage = 0;
	int defeatCount = 0;
	int chargeInterruptions = 0;
	std::array<int, static_cast<std::size_t>(AxisResourceKind::Count)>
		resourceGained{};
	std::array<int, static_cast<std::size_t>(AxisResourceKind::Count)>
		resourceConsumed{};
	std::uint32_t finisherMask = 0;
};

// 新しいボールを取得したことを通知するイベント
struct BallAcquiredEvent
{
	std::string ballId;	// 取得したボールの定義ID
};

// ゲーム内で通知されるイベント型を1つにまとめたvariant
using GameEvent = std::variant<
	ShotFiredEvent,
	CollisionImpactEvent,
	WallImpactEvent,
	EnemyDamageEvent,
	ChainImpactEvent,
	PocketFeedbackEvent,
	PlayerDamageEvent,
	AxisResourceChangedEvent,
	AxisAbilityResolvedEvent,
	ChargeInterruptedEvent,
	EnemyAbilityStateChangedEvent,
	ShotResolvedEvent,
	BallAcquiredEvent>;

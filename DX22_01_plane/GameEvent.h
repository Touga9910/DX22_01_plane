#pragma once

#include <SimpleMath.h>

#include <string>
#include <variant>

// プレイヤーがボールを発射したことを通知するイベント
struct ShotFiredEvent
{
	std::string ballId;	// 発射したボールの定義ID
};

// 敵へダメージが発生したことを通知するイベント
struct EnemyDamageEvent
{
	DirectX::SimpleMath::Vector3 worldPosition{};	// ダメージが発生したワールド座標
	int damage = 0;									// 与えたダメージ量
	bool defeated = false;							// このダメージで敵を撃破したか
	bool enemyEnemyCollision = false;				// 敵同士の衝突によるダメージか
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
};

// プレイヤーへダメージが発生したことを通知するイベント
struct PlayerDamageEvent
{
	std::string source;		// ダメージ原因を表す文字列
	int damage = 0;			// 受けたダメージ量
	std::string sourceId;	// ダメージ元を識別するID
};

// 新しいボールを取得したことを通知するイベント
struct BallAcquiredEvent
{
	std::string ballId;	// 取得したボールの定義ID
};

// ゲーム内で通知されるイベント型を1つにまとめたvariant
using GameEvent = std::variant<
	ShotFiredEvent,
	EnemyDamageEvent,
	ChainImpactEvent,
	PocketFeedbackEvent,
	PlayerDamageEvent,
	BallAcquiredEvent>;

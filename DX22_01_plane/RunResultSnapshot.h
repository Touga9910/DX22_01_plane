#pragma once

#include "GameTypes.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// ラン終了時にリザルトシーンへ渡す不変データ。
// このスナップショットの生成後、リザルトUIは変更可能なゲーム状態を参照しない。
struct RunResultSnapshot
{
	bool completed = false;						// ランをクリアして終了したか
	int reachedFloor = 1;						// 到達した最終フロア
	int areaProgress = 0;						// ラン終了時のエリア進行値

	int totalBattles = 0;						// 挑戦した戦闘数
	int clearedBattles = 0;						// クリアした戦闘数
	int midBossChallenges = 0;					// 中ボスへ挑戦した回数
	int midBossDefeats = 0;						// 中ボスを撃破した回数
	bool finalBossReached = false;				// 最終ボス戦へ到達したか
	bool finalBossDefeated = false;				// 最終ボスを撃破したか

	std::string finalBossId;					// 対戦した最終ボスの定義ID
	int totalShots = 0;							// ラン全体のショット回数
	int totalDamage = 0;						// ラン全体で敵へ与えた総ダメージ
	int currentTurnDamage = 0;					// 終了時点の現在ターン与ダメージ
	int maximumTurnDamage = 0;					// 1ターンで与えた最大ダメージ
	int damageTaken = 0;						// ラン全体で受けた総ダメージ
	int currentHp = 0;							// ラン終了時の現在HP
	int maxHp = 1;								// ラン終了時の最大HP

	std::uint64_t activeFrames = 0;							// ラン中に計測した有効フレーム数
	std::vector<std::string> acquiredBallIds;				// ラン中に取得したボール定義ID一覧
	std::vector<RelicType> acquiredRelics;					// ラン中に取得したレリック一覧
	std::unordered_map<std::string, int> ballShotCounts;	// ボール定義IDごとの使用回数
};

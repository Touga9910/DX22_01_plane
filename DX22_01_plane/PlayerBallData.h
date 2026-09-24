#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "BallStatus.h"

// プレイヤーボールの基本カテゴリを表す
// 個別のdefinitionIdとは別に、ビルド軸や共通処理の分類に使用
enum class BallCategory
{
	Standard,	// 標準カテゴリ
	Heavy,		// 重量カテゴリ
	Pierce,		// 貫通カテゴリ
	Bounce,		// 反射カテゴリ
	Anchor,		// アンカーカテゴリ
};

// BallCategoryを保存・データ参照用の文字列IDへ変換
// Standardおよび未定義値は"standard"を返す
inline const char* BallCategoryId(BallCategory category)
{
	switch (category)
	{
	case BallCategory::Heavy: return "heavy";
	case BallCategory::Pierce: return "pierce";
	case BallCategory::Bounce: return "bounce";
	case BallCategory::Anchor: return "anchor";
	default: return "standard";
	}
}

// 文字列が現在対応しているBallCategoryのIDか判定
inline bool IsBallCategoryId(const std::string& categoryId)
{
	return categoryId == "standard" || categoryId == "heavy" ||
		categoryId == "pierce" || categoryId == "bounce" ||
		categoryId == "anchor";
}

// 文字列IDからBallCategoryを復元
// categoryIdが空または未対応の場合は旧データ互換のためdefinitionIdも確認し、
// どちらからも判定できない場合はStandardを返す
inline BallCategory BallCategoryFromId(
	const std::string& categoryId,
	const std::string& definitionId = {})
{
	if (categoryId == "heavy") return BallCategory::Heavy;
	if (categoryId == "pierce") return BallCategory::Pierce;
	if (categoryId == "bounce") return BallCategory::Bounce;
	if (categoryId == "anchor") return BallCategory::Anchor;
	// categoryのない旧データは既存IDから安全に移行
	if (definitionId == "player_heavy") return BallCategory::Heavy;
	if (definitionId == "player_chain_impact") return BallCategory::Heavy;
	if (definitionId == "player_pierce" || definitionId == "player_refractive_pierce" ||
		definitionId == "player_trace_driver" || definitionId == "player_pierce_finisher")
		return BallCategory::Pierce;
	if (definitionId == "player_bounce" || definitionId == "player_cushion_charge" ||
		definitionId == "player_ricochet_finisher")
		return BallCategory::Bounce;
	if (definitionId == "player_anchor" || definitionId == "player_stop_shield" ||
		definitionId == "player_anchor_finisher")
		return BallCategory::Anchor;
	return BallCategory::Standard;
}

// 各強化段階で適用するボール性能一式を表す
using BallUpgradeStep = BallStatus;

// プレイヤーのデッキに含まれる、ボール1個分の実行時データ
// 定義ID・個体ID・現在性能・強化段階ごとの性能をまとめて保持
struct PlayerBallData
{
	static constexpr int MaxUpgradeLevel = 2;	// ボール1個が到達できる最大強化段階

	std::string definitionId = "player_default";					// ボール定義を識別するID
	BallCategory category = BallCategory::Standard;					// このボールが属するビルドカテゴリ
	std::uint64_t instanceId = 0;									// デッキ内の個体を識別するID(未割り当て時は0)
	BallStatus status{};											// 現在適用されているボール性能
	std::array<BallUpgradeStep, MaxUpgradeLevel> upgradeTable{};	// +1、+2で適用する固定性能
	int upgradeLevel = 0;											// 現在の強化段階

	// 現在の強化段階から、さらに強化可能か判定
	bool CanUpgrade() const
	{
		return upgradeLevel >= 0 && upgradeLevel < MaxUpgradeLevel;
	}
};

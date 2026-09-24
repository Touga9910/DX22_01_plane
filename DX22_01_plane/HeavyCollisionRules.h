#pragma once

#include <algorithm>

// 重量カテゴリのショット中に発生した敵同士衝突を蓄積し、
// フィニッシャーで追加ダメージへ変換するためのルールをまとめる
namespace HeavyCollisionRules
{
	// 1ショット中に蓄積する重量カテゴリ用の状態
	struct State
	{
		int collisionCount = 0;	// 蓄積している敵同士衝突回数
	};

	// 蓄積中の衝突回数を初期状態へ戻す
	inline void Reset(State& state)
	{
		state = {};
	}

	// 重量カテゴリのショット中であれば敵同士衝突を1回記録
	// 記録した場合はtrue、対象外のショットの場合はfalseを返す
	inline bool RecordEnemyEnemyCollision(State& state, bool heavyCategoryShot)
	{
		if (!heavyCategoryShot) return false;
		state.collisionCount = (std::min)(9999, state.collisionCount + 1);
		return true;
	}

	// フィニッシャー発動時に参照・消費した衝突数と追加ダメージを返す
	struct FinisherResult
	{
		int referenced = 0;		// 発動時点で蓄積されていた衝突数
		int consumed = 0;		// 今回の発動で実際に消費した衝突数
		int bonusDamage = 0;	// 消費数から計算した追加ダメージ
	};

	// 蓄積した衝突回数を消費してフィニッシャー追加ダメージへ変換
	// consumeAmountが0以下の場合は、蓄積している衝突回数をすべて消費
	inline FinisherResult ConsumeForFinisher(
		State& state,
		float damagePerCollision,
		int consumeAmount)
	{
		FinisherResult result;
		result.referenced = (std::max)(0, state.collisionCount);
		result.consumed = consumeAmount <= 0
			? result.referenced
			: (std::min)(result.referenced, consumeAmount);
		result.bonusDamage = (std::max)(0, static_cast<int>(
			damagePerCollision * static_cast<float>(result.consumed) + 0.5f));
		state.collisionCount -= result.consumed;
		return result;
	}
}

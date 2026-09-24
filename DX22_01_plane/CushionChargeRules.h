#pragma once

#include "Collision.h"
#include "TableConfig.h"

#include <algorithm>
#include <array>
#include <cmath>

// クッションを12区画に分けてスタック状態を共有管理
// 長辺は片側4区画ずつ、短辺は片側2区画ずつに分割
namespace CushionChargeRules
{
	using DirectX::SimpleMath::Vector3;

	static constexpr int RegionCount = 12; // 管理するクッション区画数

	// 1つのクッション区画が保持するスタック状態
	struct Charge
	{
		bool active = false;             // 現在スタックが存在し、効果対象となるか
		bool usableThisShot = false;     // 現在のショット中に消費可能なスタックか
		float speedMultiplier = 1.0f;    // この区画を強利用した際に反射速度へ掛ける倍率
		int stackCount = 0;              // 現在保持しているスタック数
		int maxStack = 3;                // この区画で保持できるスタック上限
	};

	// 壁接触によって発生したスタック処理の種類
	enum class UseKind
	{
		None,       // 生成・消費のどちらも発生しなかった
		Generated,  // 新しいスタックを生成
		Strong,     // 対応カテゴリとしてスタックを強利用
		Weak,       // 非対応カテゴリとしてスタックを弱利用
	};

	// 1回の壁接触で発生したクッションスタック処理結果
	struct ContactResult
	{
		UseKind kind = UseKind::None; // 実行された処理種別
		int generated = 0;            // 今回新しく生成できたスタック数
		int consumed = 0;             // 今回消費したスタック数
		int damageBonus = 0;          // 強利用によって得た追加ダメージ
	};

	using State = std::array<Charge, RegionCount>; // 全12区画のクッション状態

	// 接触した壁と座標から、0～11のクッション区画番号を返す
	// 長辺2面を4分割、短辺2面を2分割して番号へ割り当てる
	inline int RegionFromContact(
		const Collision::Segment& wall,
		const Vector3& contact)
	{
		const Vector3 direction = wall.end - wall.start;
		const float halfWidth = TableConfig::GetFieldWidth() * 0.5f;
		const float halfDepth = TableConfig::GetFieldDepth() * 0.5f;
		if (std::abs(direction.x) >= std::abs(direction.z))
		{
			const float normalized = std::clamp(
				(contact.x + halfWidth) / (halfWidth * 2.0f), 0.0f, 0.999999f);
			const int part = static_cast<int>(normalized * 4.0f);
			return contact.z >= 0.0f ? part : 4 + part;
		}

		const float normalized = std::clamp(
			(contact.z + halfDepth) / (halfDepth * 2.0f), 0.0f, 0.999999f);
		const int part = static_cast<int>(normalized * 2.0f);
		return contact.x < 0.0f ? 8 + part : 10 + part;
	}

	// 速度倍率が実質1.0より大きく、クッションスタック生成能力を持つ値か判定
	inline bool IsCharger(float speedMultiplier)
	{
		return speedMultiplier > 1.0001f;
	}

	// プレイヤーショット開始時、既存スタックをこのショットで利用可能な状態へ
	inline void BeginPlayerShot(State& state)
	{
		for (Charge& charge : state)
		{
			if (charge.stackCount > 0)
			{
				charge.active = true;
				charge.usableThisShot = true;
			}
		}
	}

	// ショット終了時にスタック自体は減衰させない
	// このショットで新規生成したスタックは次のショットから利用可能になり、
	// 未使用の既存スタックもそのままフィールド上へ残る
	inline void EndPlayerShot(State& state)
	{
		for (Charge& charge : state)
		{
			charge.active = charge.stackCount > 0;
			charge.usableThisShot = false;
		}
	}

	// 指定したクッション区画への接触を処理し、生成・強利用・弱利用の結果を返す
	// 区画番号が範囲外の場合は何もせず既定値を返す
	// 利用可能スタックがない場合はgenerateAmount分を生成し、生成したスタックは同一ショットでは使用できない
	// 強利用では指定数を消費して保存速度倍率と追加ダメージを適用し、弱利用では1個消費して弱利用速度倍率を適用
	inline ContactResult ApplyStackContact(
		State& state,
		int region,
		int generateAmount,
		int maximumStack,
		int consumeAmount,
		bool strongUse,
		bool allowMultipleStrongUses,
		float generatedSpeedMultiplier,
		float weakSpeedMultiplier,
		int strongDamageBonus,
		int finisherBonusPerUse,
		bool& strongUseConsumedThisShot,
		int& strongUseCountThisShot,
		Vector3& reflectedVelocity)
	{
		if (region < 0 || region >= RegionCount) return {};
		Charge& charge = state[static_cast<std::size_t>(region)];
		charge.maxStack = std::clamp(maximumStack, 1, 99);
		// すでに利用可能なスタックがある区画では接触時に必ず消費
		// 新規生成は、このショットで利用できる資源が区画に存在しない場合だけ行う
		if (!charge.active || !charge.usableThisShot || charge.stackCount <= 0)
		{
			if (generateAmount <= 0) return {};
			const int before = charge.stackCount;
			charge.stackCount = std::clamp(
				charge.stackCount + generateAmount, 0, charge.maxStack);
			charge.active = charge.stackCount > 0;
			charge.usableThisShot = false;
			charge.speedMultiplier = std::clamp(generatedSpeedMultiplier, 1.0f, 3.0f);
			return { UseKind::Generated, charge.stackCount - before, 0, 0 };
		}
		const bool applyStrong = strongUse &&
			(!strongUseConsumedThisShot || allowMultipleStrongUses);
		const int requested = applyStrong ? (std::max)(1, consumeAmount) : 1;
		const int consumed = (std::min)(charge.stackCount, requested);
		charge.stackCount -= consumed;
		charge.active = charge.stackCount > 0;
		if (!charge.active) charge.usableThisShot = false;
		if (applyStrong)
		{
			reflectedVelocity *= charge.speedMultiplier;
			strongUseConsumedThisShot = true;
			++strongUseCountThisShot;
			return { UseKind::Strong, 0, consumed,
				strongDamageBonus + finisherBonusPerUse };
		}
		reflectedVelocity *= std::clamp(weakSpeedMultiplier, 1.0f, 1.25f);
		return { UseKind::Weak, 0, consumed, 0 };
	}

	// この接触でショット中1回の強利用速度ブーストが実際に適用された場合だけtrueを返す
	inline bool ApplyPlayerWallContact(
		State& state,
		int region,
		float chargerSpeedMultiplier,
		bool& boostConsumedThisShot,
		Vector3& reflectedVelocity)
	{
		int useCount = 0;
		const ContactResult result = ApplyStackContact(
			state, region, IsCharger(chargerSpeedMultiplier) ? 1 : 0, 1, 1,
			true, false, chargerSpeedMultiplier, 1.0f, 0, 0,
			boostConsumedThisShot, useCount, reflectedVelocity);
		return result.kind == UseKind::Strong;
	}

	// スタックを1個以上保持しているクッション区画数を返す
	inline int ActiveCount(const State& state)
	{
		return static_cast<int>(std::count_if(
			state.begin(), state.end(), [](const Charge& charge) { return charge.stackCount > 0; }));
	}

	// スタックは存在するが、現在のショットではまだ利用できない区画数を返す
	inline int PendingNextShotCount(const State& state)
	{
		return static_cast<int>(std::count_if(
			state.begin(), state.end(), [](const Charge& charge)
			{
				return charge.stackCount > 0 && !charge.usableThisShot;
			}));
	}

	// 全クッション区画が保持しているスタック数の合計を返す
	inline int TotalStacks(const State& state)
	{
		int total = 0;
		for (const Charge& charge : state) total += (std::max)(0, charge.stackCount);
		return total;
	}
}

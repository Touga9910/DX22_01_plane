#pragma once
#include "BallStatus.h"
#include "json/json.hpp"
#include <cmath>

// JSONオブジェクトからBallStatusの各項目を読み込む
// valueがオブジェクトでない場合は受け取ったstatusをそのまま返す
// JSONに存在しない項目はstatus側の既存値を維持するため、旧データの読み込みにも使用できる
inline BallStatus ReadBallStatus(const nlohmann::json& value, BallStatus status = {})
{
	if (!value.is_object()) return status;
	status.attack = value.value("attack", status.attack);
	status.defense = value.value("defense", status.defense);
	status.mass = value.value("mass", status.mass);
	status.radius = value.value("radius", status.radius);
	status.restitution = value.value("restitution", status.restitution);
	status.friction = value.value("friction", status.friction);
	status.knockbackTransfer = value.value("knockbackTransfer", status.knockbackTransfer);
	status.pierceMaxUses = value.value("pierceMaxUses", status.pierceMaxUses);
	status.pierceSpeedRetention = value.value("pierceSpeedRetention", status.pierceSpeedRetention);
	status.anchorBrakeMultiplier = value.value("anchorBrakeMultiplier", status.anchorBrakeMultiplier);
	status.anchorStopSpeedSquared = value.value("anchorStopSpeedSquared", status.anchorStopSpeedSquared);
	status.anchorKnockbackImmune = value.value("anchorKnockbackImmune", status.anchorKnockbackImmune);
	status.cushionChargeSpeedMultiplier = value.value(
		"cushionChargeSpeedMultiplier", status.cushionChargeSpeedMultiplier);
	status.stopShieldAmount = value.value("stopShieldAmount", status.stopShieldAmount);
	status.chainImpactRadius = value.value("chainImpactRadius", status.chainImpactRadius);
	status.heavyFinisherDamagePerCollision = value.value("heavyFinisherDamagePerCollision", status.heavyFinisherDamagePerCollision);
	status.heavyCollisionConsumeAmount = value.value("heavyCollisionConsumeAmount", status.heavyCollisionConsumeAmount);
	status.traceDurability = value.value("traceDurability", status.traceDurability);
	status.traceUseAngleTolerance = value.value("traceUseAngleTolerance", status.traceUseAngleTolerance);
	status.traceUseDistance = value.value("traceUseDistance", status.traceUseDistance);
	status.traceWidth = value.value("traceWidth", status.traceWidth);
	status.tracePierceSpeedMultiplier = value.value("tracePierceSpeedMultiplier", status.tracePierceSpeedMultiplier);
	status.tracePierceAttackBonus = value.value("tracePierceAttackBonus", status.tracePierceAttackBonus);
	status.tracePierceMaxUsesBonus = value.value("tracePierceMaxUsesBonus", status.tracePierceMaxUsesBonus);
	status.tracePierceSpeedRetentionBonus = value.value("tracePierceSpeedRetentionBonus", status.tracePierceSpeedRetentionBonus);
	status.traceNonPierceSpeedMultiplier = value.value("traceNonPierceSpeedMultiplier", status.traceNonPierceSpeedMultiplier);
	status.pierceFinisherBaseBonus = value.value("pierceFinisherBaseBonus", status.pierceFinisherBaseBonus);
	status.pierceFinisherMultiTargetBonus = value.value("pierceFinisherMultiTargetBonus", status.pierceFinisherMultiTargetBonus);
	status.cushionStackGenerateAmount = value.value("cushionStackGenerateAmount", status.cushionStackGenerateAmount);
	status.cushionMaxStack = value.value("cushionMaxStack", status.cushionMaxStack);
	status.cushionStackConsumeAmount = value.value("cushionStackConsumeAmount", status.cushionStackConsumeAmount);
	status.cushionBounceAttackBonus = value.value("cushionBounceAttackBonus", status.cushionBounceAttackBonus);
	status.cushionNonBounceSpeedMultiplier = value.value("cushionNonBounceSpeedMultiplier", status.cushionNonBounceSpeedMultiplier);
	status.ricochetFinisherBonusPerUse = value.value("ricochetFinisherBonusPerUse", status.ricochetFinisherBonusPerUse);
	status.anchorPlayerStackGenerate = value.value("anchorPlayerStackGenerate", status.anchorPlayerStackGenerate);
	status.anchorEnemyStackGenerate = value.value("anchorEnemyStackGenerate", status.anchorEnemyStackGenerate);
	status.anchorStackRadius = value.value("anchorStackRadius", status.anchorStackRadius);
	status.anchorStackMax = value.value("anchorStackMax", status.anchorStackMax);
	status.anchorFinisherStackConsume = value.value("anchorFinisherStackConsume", status.anchorFinisherStackConsume);
	status.anchorFinisherDamagePerStack = value.value("anchorFinisherDamagePerStack", status.anchorFinisherDamagePerStack);
	status.anchorFinisherAoeThreshold = value.value("anchorFinisherAoeThreshold", status.anchorFinisherAoeThreshold);
	status.anchorFinisherAoeRadius = value.value("anchorFinisherAoeRadius", status.anchorFinisherAoeRadius);
	if (value.contains("abilities") && value["abilities"].is_object())
	{
		const auto& abilities = value["abilities"];
		status.abilities.split = abilities.value("split", status.abilities.split);
		status.abilities.pierce = abilities.value("pierce", status.abilities.pierce);
		status.abilities.anchor = abilities.value("anchor", status.abilities.anchor);
		status.abilities.refractAfterPierce = abilities.value(
			"refractAfterPierce", status.abilities.refractAfterPierce);
	}
	return status;
}

// BallStatusの全共通項目をJSONオブジェクトへ変換
// 敵・デバッグ用途も含む汎用形式のためdefenseも出力
inline nlohmann::json WriteBallStatus(const BallStatus& status)
{
	return {
		{ "attack", status.attack },
		{ "defense", status.defense },
		{ "mass", status.mass },
		{ "radius", status.radius },
		{ "restitution", status.restitution },
		{ "friction", status.friction },
		{ "knockbackTransfer", status.knockbackTransfer },
		{ "pierceMaxUses", status.pierceMaxUses },
		{ "pierceSpeedRetention", status.pierceSpeedRetention },
		{ "anchorBrakeMultiplier", status.anchorBrakeMultiplier },
		{ "anchorStopSpeedSquared", status.anchorStopSpeedSquared },
		{ "anchorKnockbackImmune", status.anchorKnockbackImmune },
		{ "cushionChargeSpeedMultiplier", status.cushionChargeSpeedMultiplier },
		{ "stopShieldAmount", status.stopShieldAmount },
		{ "chainImpactRadius", status.chainImpactRadius },
		{ "heavyFinisherDamagePerCollision", status.heavyFinisherDamagePerCollision },
		{ "heavyCollisionConsumeAmount", status.heavyCollisionConsumeAmount },
		{ "traceDurability", status.traceDurability },
		{ "traceUseAngleTolerance", status.traceUseAngleTolerance },
		{ "traceUseDistance", status.traceUseDistance },
		{ "traceWidth", status.traceWidth },
		{ "tracePierceSpeedMultiplier", status.tracePierceSpeedMultiplier },
		{ "tracePierceAttackBonus", status.tracePierceAttackBonus },
		{ "tracePierceMaxUsesBonus", status.tracePierceMaxUsesBonus },
		{ "tracePierceSpeedRetentionBonus", status.tracePierceSpeedRetentionBonus },
		{ "traceNonPierceSpeedMultiplier", status.traceNonPierceSpeedMultiplier },
		{ "pierceFinisherBaseBonus", status.pierceFinisherBaseBonus },
		{ "pierceFinisherMultiTargetBonus", status.pierceFinisherMultiTargetBonus },
		{ "cushionStackGenerateAmount", status.cushionStackGenerateAmount },
		{ "cushionMaxStack", status.cushionMaxStack },
		{ "cushionStackConsumeAmount", status.cushionStackConsumeAmount },
		{ "cushionBounceAttackBonus", status.cushionBounceAttackBonus },
		{ "cushionNonBounceSpeedMultiplier", status.cushionNonBounceSpeedMultiplier },
		{ "ricochetFinisherBonusPerUse", status.ricochetFinisherBonusPerUse },
		{ "anchorPlayerStackGenerate", status.anchorPlayerStackGenerate },
		{ "anchorEnemyStackGenerate", status.anchorEnemyStackGenerate },
		{ "anchorStackRadius", status.anchorStackRadius },
		{ "anchorStackMax", status.anchorStackMax },
		{ "anchorFinisherStackConsume", status.anchorFinisherStackConsume },
		{ "anchorFinisherDamagePerStack", status.anchorFinisherDamagePerStack },
		{ "anchorFinisherAoeThreshold", status.anchorFinisherAoeThreshold },
		{ "anchorFinisherAoeRadius", status.anchorFinisherAoeRadius },
		{ "abilities", { { "split", status.abilities.split }, { "pierce", status.abilities.pierce }, { "anchor", status.abilities.anchor },
			{ "refractAfterPierce", status.abilities.refractAfterPierce } } },
	};
}

// BallStatusの数値が保存・実行時に許容する範囲内か検証
// NaNや無限大を含む不正な浮動小数値もfalseとする
inline bool IsValidBallStatus(const BallStatus& s)
{
	return s.attack >= 0 && s.attack <= 100000 && s.defense >= 0 && s.defense <= 100000 &&
		std::isfinite(s.mass) && s.mass > 0.0f && s.mass <= 1000.0f &&
		std::isfinite(s.radius) && s.radius >= 0.0f && s.radius <= 100.0f &&
		std::isfinite(s.restitution) && s.restitution >= 0.0f && s.restitution <= 1.0f &&
		std::isfinite(s.friction) && s.friction >= 0.0f && s.friction <= 10.0f &&
		std::isfinite(s.knockbackTransfer) && s.knockbackTransfer >= 0.0f && s.knockbackTransfer <= 3.0f &&
		s.pierceMaxUses >= 0 && s.pierceMaxUses <= 16 &&
		std::isfinite(s.pierceSpeedRetention) && s.pierceSpeedRetention >= 0.0f && s.pierceSpeedRetention <= 1.0f &&
		std::isfinite(s.anchorBrakeMultiplier) && s.anchorBrakeMultiplier >= 1.0f && s.anchorBrakeMultiplier <= 5.0f &&
		std::isfinite(s.anchorStopSpeedSquared) && s.anchorStopSpeedSquared >= 0.03f && s.anchorStopSpeedSquared <= 1.0f &&
		std::isfinite(s.cushionChargeSpeedMultiplier) && s.cushionChargeSpeedMultiplier >= 1.0f &&
		s.cushionChargeSpeedMultiplier <= 3.0f &&
		s.stopShieldAmount >= 0 && s.stopShieldAmount <= 100 &&
		std::isfinite(s.chainImpactRadius) && s.chainImpactRadius >= 0.0f &&
		s.chainImpactRadius <= 100.0f &&
		std::isfinite(s.heavyFinisherDamagePerCollision) && s.heavyFinisherDamagePerCollision >= 0.0f && s.heavyFinisherDamagePerCollision <= 100.0f &&
		s.heavyCollisionConsumeAmount >= 0 && s.heavyCollisionConsumeAmount <= 9999 &&
		s.traceDurability >= 0 && s.traceDurability <= 99 &&
		std::isfinite(s.traceUseAngleTolerance) && s.traceUseAngleTolerance >= 1.0f && s.traceUseAngleTolerance <= 89.0f &&
		std::isfinite(s.traceUseDistance) && s.traceUseDistance >= 0.1f && s.traceUseDistance <= 200.0f &&
		std::isfinite(s.traceWidth) && s.traceWidth >= 0.1f && s.traceWidth <= 50.0f &&
		std::isfinite(s.tracePierceSpeedMultiplier) && s.tracePierceSpeedMultiplier >= 1.0f && s.tracePierceSpeedMultiplier <= 3.0f &&
		s.tracePierceAttackBonus >= 0 && s.tracePierceAttackBonus <= 1000 &&
		s.tracePierceMaxUsesBonus >= 0 && s.tracePierceMaxUsesBonus <= 16 &&
		std::isfinite(s.tracePierceSpeedRetentionBonus) && s.tracePierceSpeedRetentionBonus >= 0.0f && s.tracePierceSpeedRetentionBonus <= 1.0f &&
		std::isfinite(s.traceNonPierceSpeedMultiplier) && s.traceNonPierceSpeedMultiplier >= 1.0f && s.traceNonPierceSpeedMultiplier <= 1.25f &&
		s.pierceFinisherBaseBonus >= 0 && s.pierceFinisherBaseBonus <= 1000 &&
		s.pierceFinisherMultiTargetBonus >= 0 && s.pierceFinisherMultiTargetBonus <= 1000 &&
		s.cushionStackGenerateAmount >= 0 && s.cushionStackGenerateAmount <= 99 &&
		s.cushionMaxStack >= 1 && s.cushionMaxStack <= 99 &&
		s.cushionStackConsumeAmount >= 1 && s.cushionStackConsumeAmount <= 99 &&
		s.cushionBounceAttackBonus >= 0 && s.cushionBounceAttackBonus <= 1000 &&
		std::isfinite(s.cushionNonBounceSpeedMultiplier) && s.cushionNonBounceSpeedMultiplier >= 1.0f && s.cushionNonBounceSpeedMultiplier <= 1.25f &&
		s.ricochetFinisherBonusPerUse >= 0 && s.ricochetFinisherBonusPerUse <= 1000 &&
		s.anchorPlayerStackGenerate >= 0 && s.anchorPlayerStackGenerate <= 99 &&
		s.anchorEnemyStackGenerate >= 0 && s.anchorEnemyStackGenerate <= 99 &&
		std::isfinite(s.anchorStackRadius) && s.anchorStackRadius >= 0.0f && s.anchorStackRadius <= 100.0f &&
		s.anchorStackMax >= 1 && s.anchorStackMax <= 9999 &&
		s.anchorFinisherStackConsume >= 0 && s.anchorFinisherStackConsume <= 9999 &&
		s.anchorFinisherDamagePerStack >= 0 && s.anchorFinisherDamagePerStack <= 1000 &&
		s.anchorFinisherAoeThreshold >= 0 && s.anchorFinisherAoeThreshold <= 9999 &&
		std::isfinite(s.anchorFinisherAoeRadius) && s.anchorFinisherAoeRadius >= 0.0f && s.anchorFinisherAoeRadius <= 100.0f;
}

// -------------------------
// プレイヤーボール用互換処理
// -------------------------
// プレイヤーボールでは防御力を使用しない
// 汎用BallStatusには敵・デバッグ互換のためdefenseを残し、以下の関数で常に0として扱う
// プレイヤーボール用にBallStatusを読み込み、defenseを必ず0へ補正して返す
inline BallStatus ReadPlayerBallStatus(
	const nlohmann::json& value,
	BallStatus status = {})
{
	status = ReadBallStatus(value, status);
	status.defense = 0;
	return status;
}

// プレイヤーボール用JSONを生成し、汎用形式からdefense項目を除外して返す
inline nlohmann::json WritePlayerBallStatus(const BallStatus& status)
{
	nlohmann::json value = WriteBallStatus(status);
	value.erase("defense");
	return value;
}

// defenseを0として扱ったうえで、プレイヤーボールのBallStatusが有効範囲か検証する。
inline bool IsValidPlayerBallStatus(BallStatus status)
{
	status.defense = 0;
	return IsValidBallStatus(status);
}

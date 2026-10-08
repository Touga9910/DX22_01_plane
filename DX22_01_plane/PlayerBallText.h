#pragma once

#include "PlayerBallData.h"

#include <string>
#include <array>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace PlayerBallText
{
	inline const char* Utf8(const char8_t* text) noexcept
	{
		return reinterpret_cast<const char*>(text);
	}

	inline std::array<float, 3> GetColor(const std::string& id)
	{
		if (id == "player_heavy" || id == "player_collision_boost" || id == "player_lightweight") return { 0.85f, 0.45f, 0.16f };
		if (id == "player_pierce" || id == "player_refractive_pierce" || id == "player_trace_driver" || id == "player_pierce_finisher" || id == "player_minion_finisher") return { 0.64f, 0.38f, 0.96f };
		if (id == "player_bounce" || id == "player_cushion_charge" || id == "player_ricochet_finisher" || id == "player_reflection_setup") return { 0.98f, 0.81f, 0.18f };
		if (id == "player_anchor" || id == "player_stop_shield" || id == "player_anchor_finisher" || id == "player_origin_anchor") return { 0.20f, 0.78f, 0.64f };
		return { 0.25f, 0.63f, 1.0f };
	}

	inline std::array<float, 3> GetColor(const PlayerBallData& ball)
	{
		switch (ball.category)
		{
		case BallCategory::Heavy: return { 0.85f, 0.45f, 0.16f };
		case BallCategory::Pierce: return { 0.64f, 0.38f, 0.96f };
		case BallCategory::Bounce: return { 0.98f, 0.81f, 0.18f };
		case BallCategory::Anchor: return { 0.20f, 0.78f, 0.64f };
		default: return { 0.25f, 0.63f, 1.0f };
		}
	}

	inline const char* GetName(const std::string& definitionId)
	{
		if (definitionId == "player_standard")
		{
			return Utf8(u8"\u30b9\u30bf\u30f3\u30c0\u30fc\u30c9\u7403");
		}
		if (definitionId == "player_heavy")
		{
			return Utf8(u8"\u30d8\u30d3\u30fc\u7403");
		}
		if (definitionId == "player_pierce")
		{
			return Utf8(u8"\u8cab\u901a\u7403");
		}
		if (definitionId == "player_bounce")
		{
			return Utf8(u8"\u30d0\u30a6\u30f3\u30c9\u7403");
		}
		if (definitionId == "player_anchor")
		{
			return Utf8(u8"\u30a2\u30f3\u30ab\u30fc\u7403");
		}
		if (definitionId == "player_cushion_charge")
		{
			return Utf8(u8"\u30af\u30c3\u30b7\u30e7\u30f3\u84c4\u7a4d\u7403");
		}
		if (definitionId == "player_chain_impact") return Utf8(u8"\u9023\u9396\u885d\u6483\u7403");
		if (definitionId == "player_refractive_pierce") return Utf8(u8"\u5c48\u6298\u8cab\u901a\u7403");
		if (definitionId == "player_stop_shield") return Utf8(u8"\u505c\u6b62\u6642\u30b7\u30fc\u30eb\u30c9\u7403");
		if (definitionId == "player_trace_driver") return Utf8(u8"射線展開球");
		if (definitionId == "player_pierce_finisher") return Utf8(u8"貫通決着球");
		if (definitionId == "player_ricochet_finisher") return Utf8(u8"連続跳弾球");
		if (definitionId == "player_anchor_finisher") return Utf8(u8"錨回収球");
		if (definitionId == "player_collision_boost") return Utf8(u8"衝突強化球");
		if (definitionId == "player_lightweight") return Utf8(u8"軽量化球");
		if (definitionId == "player_minion_finisher") return Utf8(u8"ミニオン展開球");
		if (definitionId == "player_reflection_setup") return Utf8(u8"反射セットアップ球");
		if (definitionId == "player_origin_anchor") return Utf8(u8"始点固定球");
		return Utf8(u8"\u540d\u79f0\u672a\u8a2d\u5b9a\u306e\u30dc\u30fc\u30eb");
	}

	inline const char* GetDescription(const std::string& definitionId)
	{
		if (definitionId == "player_standard") return Utf8(u8"\u6271\u3044\u3084\u3059\u3044\u57fa\u672c\u306e\u30dc\u30fc\u30eb\u3002\u5f37\u5316\u3067\u653b\u6483\u529b\u304c\u4e0a\u304c\u308b\u3002");
		if (definitionId == "player_heavy") return Utf8(u8"敵を強く吹き飛ばし、敵ごとの移動距離から重量スタックを生成する。");
		if (definitionId == "player_pierce") return Utf8(u8"敵を動かさず貫通し、貫通スタックと始点・方向を記録した貫通痕を残す。");
		if (definitionId == "player_bounce") return Utf8(u8"壁反射が得意な利用球。生成済みのクッションスタックを消費し、強く加速して攻撃力を得る。");
		if (definitionId == "player_anchor") return Utf8(u8"敵に当たるとその場で停止し、接触した対象にEnemy Anchorを集中付与する。");
		if (definitionId == "player_cushion_charge") return Utf8(u8"進行方向が変わる有効な壁反射ごとに、共有の反射スタックを生成する。");
		if (definitionId == "player_chain_impact") return Utf8(u8"重量スタックを最大5消費し、命中した敵の周囲へ追加ダメージを連鎖させる。");
		if (definitionId == "player_refractive_pierce") return Utf8(u8"敵を動かさず貫通し、本来敵が飛ぶ方向へ自球の進行方向を屈折させる。");
		if (definitionId == "player_stop_shield") return Utf8(u8"\u65e9\u304f\u505c\u6b62\u3057\u3001\u6b21\u306e\u6575\u653b\u6483\u4e2d\u3060\u3051\u30c0\u30e1\u30fc\u30b8\u3092\u5438\u53ce\u3059\u308b\u30b7\u30fc\u30eb\u30c9\u3092\u5f35\u308b\u30dc\u30fc\u30eb\u3002\u5f37\u5316\u3067\u5438\u53ce\u91cf\u304c\u4e0a\u304c\u308b\u3002");
		if (definitionId == "player_trace_driver") return Utf8(u8"貫通スタックを自動消費し、新しい貫通痕から順にミニオンを発射する。");
		if (definitionId == "player_pierce_finisher") return Utf8(u8"同じショットで異なる敵を多く貫通するほど追加ダメージが増える。");
		if (definitionId == "player_ricochet_finisher") return Utf8(u8"反射スタックを消費し、最初に接触した敵1体をピンボール状態にする。");
		if (definitionId == "player_anchor_finisher") return Utf8(u8"接触した敵のEnemy Anchorを全消費し、Player Anchor量に応じた追加ダメージへ変換する。");
		if (definitionId == "player_collision_boost") return Utf8(u8"重量スタックを消費し、次の1ショットだけ敵同士の衝突ダメージを強化する。");
		if (definitionId == "player_lightweight") return Utf8(u8"接触した敵を次のノックバック1回だけ大きく吹き飛ばし、敵ごとに重量スタックを少量生成する。");
		if (definitionId == "player_minion_finisher") return Utf8(u8"貫通スタックを全消費し、新しい貫通痕から順に最大3体のミニオンを展開する。");
		if (definitionId == "player_reflection_setup") return Utf8(u8"次に発射するボールの最初の有効な壁反射で反射スタックを得る。");
		if (definitionId == "player_origin_anchor") return Utf8(u8"敵を吹き飛ばさず印を付け、次ターン開始時にショット開始位置へ戻る。");
		return Utf8(u8"\u30dc\u30fc\u30eb\u306e\u8aac\u660e\u306f\u672a\u767b\u9332\u3067\u3059\u3002");
	}

	inline const char* GetTrait(const PlayerBallData& ball)
	{
		switch (ball.category)
		{
		case BallCategory::Heavy: return Utf8(u8"\u91cd\u91cf");
		case BallCategory::Pierce: return Utf8(u8"\u8cab\u901a");
		case BallCategory::Bounce: return Utf8(u8"\u53cd\u767a");
		case BallCategory::Anchor: return Utf8(u8"\u30a2\u30f3\u30ab\u30fc");
		default: return Utf8(u8"\u6c4e\u7528");
		}
	}

	inline const char* GetRoles(const std::string& definitionId)
	{
		if (definitionId == "player_heavy" || definitionId == "player_pierce" ||
			definitionId == "player_refractive_pierce" || definitionId == "player_cushion_charge" ||
			definitionId == "player_anchor") return Utf8(u8"[生成]");
		if (definitionId == "player_stop_shield") return Utf8(u8"[生成] [防御補助]");
		if (definitionId == "player_trace_driver") return Utf8(u8"[利用] [展開]");
		if (definitionId == "player_chain_impact" || definitionId == "player_pierce_finisher" ||
			definitionId == "player_ricochet_finisher" || definitionId == "player_anchor_finisher")
			return Utf8(u8"[利用] [決着]");
		if (definitionId == "player_bounce") return Utf8(u8"[利用]");
		if (definitionId == "player_collision_boost") return Utf8(u8"[利用] [準備]");
		if (definitionId == "player_lightweight" || definitionId == "player_origin_anchor" || definitionId == "player_reflection_setup") return Utf8(u8"[準備]");
		if (definitionId == "player_minion_finisher") return Utf8(u8"[利用] [決着]");
		return Utf8(u8"[汎用]");
	}

	inline const char* GetShortDescription(const std::string& definitionId)
	{
		if (definitionId == "player_heavy") return Utf8(u8"敵を強く吹き飛ばし、移動距離で重量スタックを生成。");
		if (definitionId == "player_chain_impact") return Utf8(u8"重量スタックを部分消費して範囲ダメージへ変換。");
		if (definitionId == "player_pierce" || definitionId == "player_refractive_pierce") return Utf8(u8"敵を貫通し、通った軌道に貫通痕を生成。");
		if (definitionId == "player_trace_driver") return Utf8(u8"貫通スタックをミニオンへ変換。");
		if (definitionId == "player_pierce_finisher") return Utf8(u8"同一ショットの複数貫通を決着ダメージへ変換。");
		if (definitionId == "player_cushion_charge") return Utf8(u8"有効な壁反射で共有の反射スタックを生成。");
		if (definitionId == "player_bounce") return Utf8(u8"クッションスタックを強利用して加速。");
		if (definitionId == "player_ricochet_finisher") return Utf8(u8"1ショットで複数スタックを消費して決着。");
		if (definitionId == "player_anchor" || definitionId == "player_stop_shield") return Utf8(u8"停止時に錨スタックを生成。");
		if (definitionId == "player_anchor_finisher") return Utf8(u8"回収した錨スタックを決着ダメージへ変換。");
		if (definitionId == "player_collision_boost") return Utf8(u8"次球の敵同士衝突ダメージを強化。");
		if (definitionId == "player_lightweight") return Utf8(u8"次のノックバックを強化し、敵ごとに重量+1。");
		if (definitionId == "player_minion_finisher") return Utf8(u8"全貫通スタックを最大3体のミニオンへ変換。");
		if (definitionId == "player_reflection_setup") return Utf8(u8"次球の最初の反射で反射スタックを獲得。");
		if (definitionId == "player_origin_anchor") return Utf8(u8"次ターンに始点へ戻り、敵へ少量の印を付与。");
		return Utf8(u8"扱いやすく、攻撃力に優れる基本球。");
	}
	inline std::string GetStats(const PlayerBallData& ball, const BallStatus& status)
	{
		std::ostringstream text;
		if (ball.definitionId == "player_cushion_charge")
			text << Utf8(u8"有効反射 ") << status.cushionReflectionLimit
				<< Utf8(u8"回まで / 1反射 +") << status.cushionStackGenerateAmount;
		else if (ball.definitionId == "player_chain_impact")
			text << Utf8(u8"重量スタックを最大5消費 / 連鎖範囲を強化");
		else if (ball.definitionId == "player_refractive_pierce")
			text << Utf8(u8"敵を動かさず3体まで屈折貫通 / 速度を失いにくい");
		else if (ball.definitionId == "player_stop_shield")
			text << Utf8(u8"Enemy Anchorを最大5消費 / Shield最大 ")
				<< status.stopShieldAmount;
		else if (ball.definitionId == "player_trace_driver")
			text << Utf8(u8"貫通スタックを最大 ") << status.pierceStackConsumeLimit
				<< Utf8(u8" 自動消費");
		else if (ball.definitionId == "player_pierce_finisher")
			text << Utf8(u8"異なる敵を貫通するほど追加ダメージ");
		else if (ball.definitionId == "player_ricochet_finisher")
			text << Utf8(u8"反射スタック6消費 / 最初の敵をピンボール化");
		else if (ball.definitionId == "player_anchor_finisher")
			text << Utf8(u8"対象のEnemy Anchorを全消費 / 一部を周囲へ波及");
		else if (ball.definitionId == "player_origin_anchor")
			text << Utf8(u8"敵を吹き飛ばさない / 次ターンに始点へ復帰");
		else if (ball.definitionId == "player_lightweight")
			text << Utf8(u8"次のノックバック1回を強化 / 敵ごとに重量+1");
		else if (ball.definitionId == "player_collision_boost")
			text << Utf8(u8"次球の敵同士衝突だけを強化");
		else if (ball.definitionId == "player_reflection_setup")
			text << Utf8(u8"次球の最初の有効反射で反射スタック +")
				<< status.reflectionSetupAmount;
		else if (ball.category == BallCategory::Heavy)
			text << Utf8(u8"敵を強く吹き飛ばす");
		else if (ball.category == BallCategory::Pierce)
			text << Utf8(u8"敵を動かさず3体まで貫通 / 速度を失いにくい");
		else if (ball.category == BallCategory::Bounce)
			text << Utf8(u8"有効な壁反射を攻撃へ変換");
		else if (ball.category == BallCategory::Anchor)
			text << Utf8(u8"敵に当たるとその場で停止");
		else
			text << Utf8(u8"扱いやすい標準物理");
		return text.str();
	}

	inline std::string GetUpgradePreview(const PlayerBallData& ball)
	{
		if (!ball.CanUpgrade()) return Utf8(u8"\u6700\u5927\u5f37\u5316");
		return std::string(Utf8(u8"\u73fe\u5728\uff1a")) + GetStats(ball, ball.status) + "\n" +
			Utf8(u8"\u5f37\u5316\u5f8c\uff1a") + GetStats(ball, ball.upgradeTable[ball.upgradeLevel]);
	}

}

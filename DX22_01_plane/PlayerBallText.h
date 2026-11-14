#pragma once

#include "PlayerBallData.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>

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

    inline const char* GetName(const std::string& id)
    {
        if (id == "player_standard") return Utf8(u8"スタンダード球");
        if (id == "player_heavy") return Utf8(u8"ヘビー球");
        if (id == "player_pierce") return Utf8(u8"貫通球");
        if (id == "player_bounce") return Utf8(u8"バウンド球");
        if (id == "player_anchor") return Utf8(u8"アンカー球");
        if (id == "player_cushion_charge") return Utf8(u8"クッション蓄積球");
        if (id == "player_chain_impact") return Utf8(u8"連鎖衝撃球");
        if (id == "player_refractive_pierce") return Utf8(u8"屈折貫通球");
        if (id == "player_stop_shield") return Utf8(u8"停止時シールド球");
        if (id == "player_trace_driver") return Utf8(u8"射線展開球");
        if (id == "player_pierce_finisher") return Utf8(u8"貫通決着球");
        if (id == "player_ricochet_finisher") return Utf8(u8"連続跳弾球");
        if (id == "player_anchor_finisher") return Utf8(u8"錨回収球");
        if (id == "player_collision_boost") return Utf8(u8"衝突強化球");
        if (id == "player_lightweight") return Utf8(u8"軽量化球");
        if (id == "player_minion_finisher") return Utf8(u8"ミニオン展開球");
        if (id == "player_reflection_setup") return Utf8(u8"反射セットアップ球");
        if (id == "player_origin_anchor") return Utf8(u8"始点固定球");
        return Utf8(u8"名称未設定のボール");
    }

    // 詳細ウィンドウやマウスオーバーで表示する説明。改修済みの実効果を反映。
    inline const char* GetDescription(const std::string& id)
    {
        if (id == "player_standard") return Utf8(u8"特殊効果を持たない基本球。強化ごとに攻撃力が上昇する。");
        if (id == "player_heavy") return Utf8(u8"敵を強く吹き飛ばす重量球。敵の移動距離から重量スタックを生成し、強化で攻撃力・質量・押し出し性能が上がる。");
        if (id == "player_pierce") return Utf8(u8"敵を貫通してスタックと貫通痕を作る。強化で攻撃力と貫通後の速度維持率が上がる。");
        if (id == "player_bounce") return Utf8(u8"有効な壁反射時に反射スタックを消費して加速し、その後の敵への攻撃を強化する。強化で基本攻撃力と追加ダメージ・利用回数が増える。");
        if (id == "player_anchor") return Utf8(u8"接触した敵にアンカースタックを付与する。自分のアンカースタックが多いほど追加付与しやすくなる。強化で基本攻撃力も上昇する。");
        if (id == "player_cushion_charge") return Utf8(u8"有効な壁反射で共有の反射スタックを生成する。強化で生成可能回数や1回あたりの生成量が増える。");
        if (id == "player_chain_impact") return Utf8(u8"重量スタックを最大5個消費。命中した敵の周囲から近い最大2体に衝撃波ダメージを与える。消費1個につき衝撃波ダメージが+1。強化で攻撃力と範囲が上昇する。");
        if (id == "player_refractive_pierce") return Utf8(u8"敵を貫通した後、通常衝突時に敵が飛ぶ方向へ進行方向を変える。貫通痕とスタックも生成し、強化で攻撃力と速度維持率が上がる。");
        if (id == "player_stop_shield") return Utf8(u8"アンカースタックを持つ敵への接触時、その敵のスタックを最大5個消費してシールドに変換する。反射軸の球がデッキにあれば、変換成功時に反射スタック1も生成する。");
        if (id == "player_trace_driver") return Utf8(u8"ショット開始時、貫通痕からミニオンを発射する。強化で攻撃力、押し出し倍率、消費上限3→6→9が上がる。貫通痕の数と実効強化に見合う量だけ消費する。");
        if (id == "player_pierce_finisher") return Utf8(u8"同じショットで貫通した異なる敵の数に応じ、接触ダメージに追加ボーナスを得る。強化で基本攻撃力と追加ダメージ・速度維持率が上昇する。");
        if (id == "player_ricochet_finisher") return Utf8(u8"反射スタック6個を消費して、最初に触れた敵をピンボール状態にする。対象が衝突を重ねるほど追加ダメージが増える。強化で基本攻撃力が上がり、+2では成長に必要な衝突回数が減る。");
        if (id == "player_anchor_finisher") return Utf8(u8"最初に接触したアンカースタック保有敵から、スタックを全消費して直接追加ダメージに変換する。一定数以上の消費で周囲にもダメージを与える。強化で攻撃力・範囲・発動条件が改善する。");
        if (id == "player_collision_boost") return Utf8(u8"重量スタック3個で次ショットの敵同士衝突を強化し、不足時も1.10倍を準備する。消費成功時に反射軸がデッキにあれば反射スタック1も生成する。");
        if (id == "player_lightweight") return Utf8(u8"接触した敵の次のノックバックを強化し、敵ごとに重量スタック1を生成する。貫通軸がデッキにあれば貫通スタック1も生成する。");
        if (id == "player_minion_finisher") return Utf8(u8"ショット開始時、貫通痕から最大3体のミニオンを発射する。効果のない過剰なスタックは消費せず、10～15スタックの有効消費でミニオンをさらに強化する。自身も敵を貫通でき、強化で基本攻撃力と速度維持率が上昇する。");
        if (id == "player_reflection_setup") return Utf8(u8"次のショットの最初の有効反射で反射スタックを追加する。重量軸がデッキにあれば、発動成功時に重量スタック1も生成する。");
        if (id == "player_origin_anchor") return Utf8(u8"敵を吹き飛ばさずに錨を付与し、次ターンに始点へ戻る。貫通軸がデッキにあれば復帰成功時に貫通スタック1を生成。強化で次の錨付与も増やす。");
        return Utf8(u8"ボールの説明は未登録です。");
    }

    inline const char* GetTrait(const PlayerBallData& ball)
    {
        switch (ball.category)
        {
        case BallCategory::Heavy: return Utf8(u8"重量");
        case BallCategory::Pierce: return Utf8(u8"貫通");
        case BallCategory::Bounce: return Utf8(u8"反発");
        case BallCategory::Anchor: return Utf8(u8"アンカー");
        default: return Utf8(u8"汎用");
        }
    }

    inline const char* GetRoles(const std::string& id)
    {
        if (id == "player_heavy" || id == "player_pierce" || id == "player_refractive_pierce" || id == "player_cushion_charge" || id == "player_anchor") return Utf8(u8"[生成]");
        if (id == "player_stop_shield") return Utf8(u8"[生成] [防御補助]");
        if (id == "player_trace_driver") return Utf8(u8"[利用] [展開]");
        if (id == "player_chain_impact" || id == "player_pierce_finisher" || id == "player_ricochet_finisher" || id == "player_anchor_finisher") return Utf8(u8"[利用] [決着]");
        if (id == "player_bounce") return Utf8(u8"[利用]");
        if (id == "player_collision_boost") return Utf8(u8"[利用] [準備]");
        if (id == "player_lightweight" || id == "player_origin_anchor" || id == "player_reflection_setup") return Utf8(u8"[準備]");
        if (id == "player_minion_finisher") return Utf8(u8"[利用] [決着]");
        return Utf8(u8"[汎用]");
    }

    // カード内説明は拡大表示で3～4行に収まりやすい短文にする。
    inline std::string GetShortDescription(const PlayerBallData& ball)
    {
        const BallStatus& s = ball.status;
        std::ostringstream text;
        const auto putRatio = [&text](float value)
        {
            text << std::fixed << std::setprecision(2) << value;
        };
        const std::string& id = ball.definitionId;

        if (id == "player_standard")
            text << Utf8(u8"特殊効果のない基本球。\n強化すると攻撃力が上がる。");
        else if (id == "player_heavy")
			text << Utf8(u8"敵を強く吹き飛ばす。\n敵の移動距離で重量スタック生成+")
				 << 1 + s.eventStackGenerationBonus << Utf8(u8"。");
        else if (id == "player_pierce")
			text << Utf8(u8"敵を貫通する。\n1体ごとに貫通スタック+")
				 << 1 + s.eventStackGenerationBonus << Utf8(u8"・貫通痕を生成。");
        else if (id == "player_bounce")
        {
            text << Utf8(u8"壁反射で反射スタックを消費。\n次の敵への攻撃+") << s.cushionBounceAttackBonus
                 << Utf8(u8"、速度×");
            putRatio(s.cushionReflectionSpeedMultiplier);
            text << Utf8(u8"。");
        }
        else if (id == "player_anchor")
            text << Utf8(u8"接触した敵に錨スタック+") << s.anchorEnemyStackGenerate
                 << Utf8(u8"。\n自分の錨") << s.anchorPlayerStacksPerBonus << Utf8(u8"個ごとに付与+1。");
        else if (id == "player_cushion_charge")
            text << Utf8(u8"壁反射で反射スタック+") << s.cushionStackGenerateAmount
                 << Utf8(u8"。\n1ショット最大") << s.cushionReflectionLimit << Utf8(u8"回生成。");
        else if (id == "player_chain_impact")
            text << Utf8(u8"重量を最大") << s.heavyCollisionConsumeAmount
                 << Utf8(u8"消費し衝撃波。\n周囲の敵最大2体に、消費1個で+1ダメージ。");
        else if (id == "player_refractive_pierce")
			text << Utf8(u8"敵を貫通し進行方向を変える。\n1体ごとに貫通スタック+")
				 << 1 + s.eventStackGenerationBonus << Utf8(u8"と貫通痕を生成。");
        else if (id == "player_stop_shield")
            text << Utf8(u8"接触敵の錨を最大") << s.anchorEnemyConsumeLimit
                 << Utf8(u8"消費。\n最大") << s.stopShieldAmount << Utf8(u8"シールド")
                 << Utf8(u8"。\n反射軸併用で反射+1。");
        else if (id == "player_trace_driver")
        {
            text << Utf8(u8"貫通痕からミニオンを発射。\nスタック最大") << s.pierceStackConsumeLimit
                 << Utf8(u8"消費（痕数で制限）。\n自球の押し出し×");
            putRatio(s.knockbackTransfer);
            text << Utf8(u8"。");
        }
        else if (id == "player_pierce_finisher")
        {
            const int first = s.pierceFinisherBaseBonus;
            const int step = s.pierceFinisherMultiTargetBonus;
            text << Utf8(u8"貫通した敵の数でダメージ増加。\n1/2/3体：+")
                 << first << "/+" << first + step << "/+" << first + step * 2 << Utf8(u8"。");
        }
        else if (id == "player_ricochet_finisher")
            text << Utf8(u8"反射スタック") << s.ricochetStackConsume
                 << Utf8(u8"消費で跳弾状態。\n衝突") << s.pinballCollisionsPerStage << Utf8(u8"回ごとに追加ダメージ増加。");
        else if (id == "player_anchor_finisher")
            text << Utf8(u8"接触敵の錨を全消費し追撃。\n") << s.anchorFinisherAoeThreshold
                 << Utf8(u8"以上消費で周囲にもダメージ。");
        else if (id == "player_collision_boost")
        {
            text << Utf8(u8"重量") << s.heavySetupStackConsume << Utf8(u8"消費で次球の敵同士衝突×");
            putRatio(s.heavyEnemyCollisionMultiplier);
            text << Utf8(u8"。\n不足時×1.10。\n成功+反射軸併用で反射+1。");
        }
        else if (id == "player_lightweight")
        {
            text << Utf8(u8"次の敵ノックバック×");
            putRatio(s.lightweightKnockbackMultiplier);
			text << Utf8(u8"。\n敵ごとに重量+") << 1 + s.eventStackGenerationBonus
				 << Utf8(u8"、貫通軸併用で貫通+1。");
            if (s.lightweightSpeedLossReduction > 0.0f)
                text << Utf8(u8"\n自球減速軽減")
                     << static_cast<int>(std::lround(s.lightweightSpeedLossReduction * 100.0f))
                     << Utf8(u8"%。");
        }
        else if (id == "player_minion_finisher")
            text << Utf8(u8"貫通痕から最大3体のミニオン。\n有効分だけ消費（最大15）。\n高消費でミニオン追加強化。");
        else if (id == "player_reflection_setup")
            text << Utf8(u8"次球の最初の壁反射で\n反射スタック") << s.reflectionSetupAmount << Utf8(u8"生成")
                 << Utf8(u8"。\n重量軸併用で重量+1。");
        else if (id == "player_origin_anchor")
        {
            text << Utf8(u8"敵に錨を付与、次ターン始点へ復帰。\n貫通軸併用で貫通+1。");
            if (s.nextAnchorGrantBonus > 0)
                text << Utf8(u8"\n次のアンカー付与+") << s.nextAnchorGrantBonus << Utf8(u8"。");
        }
        else
            text << Utf8(u8"ボール効果の説明は未登録。");
        return text.str();
    }

    // 強化プレビューでは段階ごとの実数値を表示する。
    inline std::string GetStats(const PlayerBallData& ball, const BallStatus& s)
    {
        std::ostringstream text;
        text << Utf8(u8"攻撃") << s.attack;
        const std::string& id = ball.definitionId;
        if (id == "player_heavy")
            text << Utf8(u8" / 質量") << std::fixed << std::setprecision(1) << s.mass
				 << Utf8(u8" / 伝達×") << std::setprecision(2) << s.knockbackTransfer
				 << Utf8(u8" / 生成+") << 1 + s.eventStackGenerationBonus;
        else if (id == "player_pierce" || id == "player_refractive_pierce" || id == "player_minion_finisher")
			text << Utf8(u8" / 貫通後速度") << std::lround(s.pierceSpeedRetention * 100.0f) << "%"
				 << Utf8(u8" / 生成+") << 1 + s.eventStackGenerationBonus;
        else if (id == "player_bounce")
            text << Utf8(u8" / 追加+") << s.cushionBounceAttackBonus
                 << Utf8(u8" / 使用上限") << s.cushionConsumptionLimit;
        else if (id == "player_anchor")
            text << Utf8(u8" / 錨付与") << s.anchorEnemyStackGenerate
                 << Utf8(u8" / 自錨") << s.anchorPlayerStacksPerBonus << Utf8(u8"個で+1");
        else if (id == "player_cushion_charge")
            text << Utf8(u8" / 壁反射") << s.cushionReflectionLimit
                 << Utf8(u8"回 / 1回+") << s.cushionStackGenerateAmount;
        else if (id == "player_chain_impact")
            text << Utf8(u8" / 範囲") << s.chainImpactRadius;
        else if (id == "player_stop_shield")
            text << Utf8(u8" / 最大シールド") << s.stopShieldAmount;
        else if (id == "player_trace_driver")
            text << Utf8(u8" / 最大消費") << s.pierceStackConsumeLimit
                 << Utf8(u8" / 自球伝達×") << std::fixed << std::setprecision(2) << s.knockbackTransfer;
        else if (id == "player_pierce_finisher")
            text << Utf8(u8" / 追加+") << s.pierceFinisherBaseBonus
				 << Utf8(u8" / 複数補正+") << s.pierceFinisherMultiTargetBonus
				 << Utf8(u8" / 生成+") << 1 + s.eventStackGenerationBonus;
        else if (id == "player_ricochet_finisher")
            text << Utf8(u8" / 衝突") << s.pinballCollisionsPerStage << Utf8(u8"回で成長");
        else if (id == "player_anchor_finisher")
            text << Utf8(u8" / 範囲") << s.anchorFinisherAoeRadius
                 << Utf8(u8" / 閾値") << s.anchorFinisherAoeThreshold;
        else if (id == "player_collision_boost")
            text << Utf8(u8" / 重量消費") << s.heavySetupStackConsume
                 << Utf8(u8" / 衝突×") << std::fixed << std::setprecision(2) << s.heavyEnemyCollisionMultiplier;
        else if (id == "player_lightweight")
            text << Utf8(u8" / 吹飛×") << std::fixed << std::setprecision(2) << s.lightweightKnockbackMultiplier
				 << Utf8(u8" / 減速軽減") << std::lround(s.lightweightSpeedLossReduction * 100.0f) << "%"
				 << Utf8(u8" / 生成+") << 1 + s.eventStackGenerationBonus;
        else if (id == "player_reflection_setup")
            text << Utf8(u8" / 次球反射+") << s.reflectionSetupAmount;
        else if (id == "player_origin_anchor")
            text << Utf8(u8" / 敵錨+") << s.anchorEnemyStackGenerate
                 << Utf8(u8" / 次回付与+") << s.nextAnchorGrantBonus;
        return text.str();
    }

    inline std::string GetUpgradePreview(const PlayerBallData& ball)
    {
        if (!ball.CanUpgrade()) return Utf8(u8"最大強化");
        return std::string(Utf8(u8"現在：")) + GetStats(ball, ball.status) + "\n" +
            Utf8(u8"強化後：") + GetStats(ball, ball.upgradeTable[ball.upgradeLevel]);
    }
}

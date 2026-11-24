#include "Game.h"
#include "GameRuntime.h"
#include "GameWorld.h"
#include "BallShotPrediction.h"
#include "GameShotPrediction.h"
#include "GamePersistenceRuntime.h"
#include "PlayerDeck.h"
#include "RunController.h"
#include "GameObject.h"
#include "BossShotPlanner.h"
#include "json/json.hpp"
#include "BallStatusJson.h"
#include "BreakBall.h"
#include "EnemyBall.h"
#include "PlayerBall.h"
#include "Pocket.h"
#include <chrono>
#include <cmath>
#include <set>
#include <map>

using namespace DirectX::SimpleMath;
using nlohmann::json;

namespace
{
    constexpr int MaximumCandidatesPerOffer = 128;
    /// <summary>
    /// ベクトルのY成分を除いて水平面に投影する。
    /// </summary>
    Vector3 Flat(Vector3 v) { v.y = 0; return v; }
    /// <summary>
    /// ベクトルを水平面に投影し、十分な長さがあれば正規化する。
    /// </summary>
    Vector3 Unit(Vector3 v) { v = Flat(v); if (v.LengthSquared() > 0.00001f) v.Normalize(); return v; }
    /// <summary>
    /// ベクトルの座標をX・Y・Z成分を持つJSONに変換する。
    /// </summary>
    json Position(const Vector3& v) { return {{"x",v.x},{"y",v.y},{"z",v.z}}; }

    // An explicitly geometric NEXT-shot opportunity, not a simulated second shot.
    /// <summary>
    /// 球の配置の幾何関係から、次のショットでボス球を押せる目安を計算する。
    /// </summary>
    float Opportunity(const Vector3& player, const Vector3& boss, const std::vector<Vector3>& neutrals)
    {
        float best = 0;
        for (const auto& neutral : neutrals)
        {
            const Vector3 push = Unit(boss-neutral), approach = Unit(neutral-player);
            const float alignment = (std::max)(0.0f, push.Dot(approach));
            const float distance = Flat(neutral-player).Length();
            best = (std::max)(best, alignment*alignment / (1.0f + distance/30.0f));
        }
        return best;
    }
    struct Aim { Vector3 velocity; std::string target, kind; };
}

/// <summary>
/// 現在のボス戦で選べる球とショットを予測し、評価順の候補と推奨案を返す。
/// </summary>
json BossShotPlanner::Evaluate(Game& game)
{
    const auto players = GameRuntime::World().GetComponents<PlayerBall>();
    EnemyBall* boss = nullptr;
    for (auto* enemy : GameRuntime::World().GetComponents<EnemyBall>())
        if (enemy != nullptr && enemy->IsArmorBoss() &&
            enemy->CanParticipateInCombat()) { boss = enemy; break; }
    if (!boss || players.empty() || !players[0]->IsIdle() || game.GetBattleState() != BattleState::AimingDirection || !game.AreAllBallsStopped()) return {};
    auto* player = players[0];
    std::string key = std::to_string(GameShotPrediction::WorldKey()) + ":" + std::to_string(game.GetTotalShotCount());
    for (int i = 0; i < game.GetBallOfferCount(); ++i)
    {
        const auto* offer = game.GetBallOffer(i);
        if (offer) key += ":" + std::to_string(offer->instanceId) + offer->definitionId + WritePlayerBallStatus(offer->status).dump();
    }
    // Expose an opaque compact key; comparison uses the complete key internally.
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : key) { hash ^= c; hash *= 1099511628211ull; }
    const std::string stateKey = std::to_string(hash);
    if (key == m_BossShotCacheKey && !m_BossShotCache.empty()) return m_BossShotCache;
    const auto started = std::chrono::steady_clock::now();
    const Vector3 start = player->GetPosition(), bossPosition = boss->GetPosition();
    struct Target { Vector3 position; float radius; std::string id; };
    std::vector<Target> neutrals;
    std::vector<Vector3> initialNeutrals;
    for (auto* minion : GameRuntime::World().GetComponents<EnemyBall>())
        if (minion->IsBossMinion() && minion->CanParticipateInCombat())
        {
            neutrals.push_back({minion->GetPosition(),minion->GetRadius(),"boss_minion:"+std::to_string(minion->GetBossMinionSerial())});
            initialNeutrals.push_back(minion->GetPosition());
        }
    const float initialOpportunity = Opportunity(start,bossPosition,initialNeutrals);
    json choices = json::array(), offerChoices = json::array();
    std::map<std::string,json> equivalentOffers;
    int evaluated = 0, incomplete = 0;
    for (int offerIndex = 0; offerIndex < game.GetBallOfferCount(); ++offerIndex)
    {
        const auto* offer = game.GetBallOffer(offerIndex);
        if (!offer) continue;
        const std::string signature = offer->definitionId + WritePlayerBallStatus(offer->status).dump();
        if (equivalentOffers.contains(signature))
        {
            auto aliases = equivalentOffers.at(signature);
            for (auto& choice : aliases)
            {
                choice["candidate_id"] = std::to_string(offerIndex)+":"+choice["candidate_id"].get<std::string>();
                choice["offer_index"] = offerIndex;
                choice["instance_id"] = offer->instanceId;
                choices.push_back(choice);
            }
            if (!aliases.empty()) offerChoices.push_back(aliases[0]);
            continue;
        }
        const auto& status = offer->status;
        std::vector<Aim> aims;
        auto add = [&](Vector3 aim, float power, const std::string& target, const std::string& kind) {
            const Vector3 delta = Flat(aim-start);
            if (delta.LengthSquared() < 0.0001f || aims.size() >= MaximumCandidatesPerOffer) return;
            const Vector3 velocity = Unit(delta)*std::clamp(power,1.0f,8.0f);
            for (const auto& other : aims) if ((other.velocity-velocity).LengthSquared() < 0.00001f) return;
            aims.push_back({velocity,target,kind});
        };
        const Vector3 side = Vector3(-Unit(bossPosition-start).z,0,Unit(bossPosition-start).x);
        for (float offset : {0.0f,-4.0f,4.0f})
            for (float power : {2.0f,4.0f,6.0f,8.0f}) add(bossPosition+side*offset,power,"boss","direct");
        for (const auto& neutral : neutrals)
        {
            const Vector3 n = neutral.position, push = Unit(bossPosition-n);
            const std::string id = neutral.id;
            for (float power : {2.0f,4.0f,6.0f}) add(n,power,id,"neutral_center");
            for (float angle : {-0.2f,0.0f,0.2f})
            {
                Vector3 direction(push.x*std::cos(angle)-push.z*std::sin(angle),0,push.x*std::sin(angle)+push.z*std::cos(angle));
                const Vector3 contact = n-direction*(status.radius+neutral.radius);
                if (Unit(contact-start).Dot(direction) > 0.15f)
                    for (float power : {2.0f,4.0f,6.0f}) add(contact,power,id,"neutral_push");
            }
            const Vector3 setup = n-push*(status.radius+neutral.radius+7.0f);
            const Vector3 lateral(-push.z,0,push.x);
            for (float offset : {0.0f,-9.0f,9.0f})
            {
                const Vector3 destination = setup+lateral*offset;
                const float brake = status.friction*(status.abilities.anchor ? status.anchorBrakeMultiplier : 1.0f);
                const float power = std::sqrt(2.0f*brake*Flat(destination-start).Length()+0.03f);
                add(destination,power,id,"setup");
            }
        }
        // Mirrors generate rail candidates; the shared solver checks actual rail gaps.
        for (const auto& wall : BallPhysicsRules::BossWalls())
        {
            Vector3 mirror = bossPosition;
            if (wall.start.x == wall.end.x) mirror.x = 2*wall.start.x-bossPosition.x;
            else mirror.z = 2*wall.start.z-bossPosition.z;
            for (float power : {4.0f,6.0f,8.0f}) add(mirror,power,"boss","bank");
        }
        for (int i=0;i<12;++i)
            for (float power : {1.0f,2.0f}) add(start+Vector3(std::cos(i*0.5235988f),0,std::sin(i*0.5235988f))*20,power,"position","reposition");
        json scored = json::array();
        for (const auto& aim : aims)
        {
            ++evaluated;
            const auto prediction = GameShotPrediction::Predict(
                *player, aim.velocity, false, offer);
            if (!prediction.complete) { ++incomplete; continue; }
            const BallShotPrediction::Ball* endPlayer = nullptr;
            const BallShotPrediction::Ball* endBoss = nullptr;
            std::vector<Vector3> remainingNeutrals;
            for (const auto& ball : prediction.balls)
            {
                if (ball.physics.player) endPlayer = &ball;
                if (ball.physics.boss) endBoss = &ball;
                if (ball.bossMinion && ball.active && !ball.defeated) remainingNeutrals.push_back(ball.physics.position);
            }
            if (!endPlayer || !endBoss) continue;
            const int damage = boss->GetHP()-endBoss->hp;
            const int direct = (std::max)(0,damage-prediction.bossFixedDamage-prediction.bossPocketDamage);
            const int armor = (std::max)(0,boss->GetBossState().armor-endBoss->bossState.armor);
            const bool startedBreak = !boss->GetBossState().IsBroken() && endBoss->bossState.IsBroken();
            const bool kill = prediction.bossEncounterCleared;
            const float opportunity = !endPlayer->pocketed && !endPlayer->defeated ?
                Opportunity(endPlayer->physics.position,endBoss->physics.position,remainingNeutrals) : 0;
            const float hpLoss = static_cast<float>((std::max)(0,player->GetHP()-endPlayer->hp));
            int minionAttack = 0;
            for (const auto& ball : prediction.balls)
                if (ball.bossMinion && ball.active && !ball.defeated && !ball.pocketed)
                    minionAttack += ball.plannedAttackDamage;
            const float incoming = static_cast<float>(BossEncounterRules::IncomingDamage(
                endBoss->bossTurn,endBoss->bossState,!kill,minionAttack,prediction.playerShieldRemaining,
                endBoss->plannedAttackDamage>0));
            const float urgency = player->GetHP() <= 10 ? 3.0f : 1.0f;
            const float followup = static_cast<float>((std::max)(0,prediction.shot.playerEnemyContacts-1));
            json metrics = {{"direct_damage",direct},{"fixed_damage",prediction.bossFixedDamage},
                {"pocket_damage",prediction.bossPocketDamage},{"pocket_count",prediction.bossPocketCount},
                {"armor_removed",armor},{"break_started",startedBreak},{"neutral_boss_hits",prediction.breakBallHits},{"minion_boss_hits",prediction.breakBallHits},
                {"next_push_opportunity_estimate",opportunity},{"opportunity_improvement",opportunity-initialOpportunity},
                {"player_hp_loss",hpLoss},{"expected_enemy_turn_damage",incoming},
                {"pierce_followup_contacts",status.abilities.pierce ? followup : 0},
                {"enemy_chain_contacts",prediction.shot.enemyEnemyContacts},{"wall_contacts",prediction.shot.wallContacts},
				{"chain_impact_hits",prediction.chainImpactHits},{"stop_shield",prediction.playerShieldRemaining},
                {"shield_granted",prediction.stopShieldGranted},{"shield_absorbed",prediction.playerShieldAbsorbed},
				{"charged_cushions",CushionChargeRules::PendingNextShotCount(prediction.cushionCharges)},
                {"anchor_stopped",prediction.shot.anchorStopped},{"boss_killed",kill},
                {"encounter_cleared",prediction.bossEncounterCleared},
                {"recovery_strong_attack_announced",prediction.bossRecoveryStrongAttackAnnounced},
                {"advance_supply_attempted_if_turn_continues",prediction.bossAdvanceSupplyAttemptedIfTurnContinues},
                {"advance_supply_requested_if_turn_continues",prediction.bossAdvanceSupplyRequestedIfTurnContinues},
                {"minions_retired_after_shot",prediction.bossMinionsRetiredAfterShot}};
            json breakdown = {
                {"direct_damage",direct*(boss->GetBossState().IsBroken() ? 1.25f : 1.0f)},
                {"fixed_damage",prediction.bossFixedDamage*1.0f},{"armor",armor*3.0f},
                {"pocket_damage",prediction.bossPocketDamage*1.0f},
                {"start_break",startedBreak ? 6.0f : 0.0f},{"kill",kill ? 100.0f : 0.0f},
                {"setup",(opportunity-initialOpportunity)*5.0f+opportunity},
                {"safety",-(hpLoss*4.0f+incoming)*urgency},
                {"fatal",endPlayer->defeated || (!kill && endPlayer->hp <= incoming) ? -1000.0f : 0.0f},
                {"wasted_break",boss->GetBossState().IsBroken() && damage==0 ? -6.0f : 0.0f},
                {"pierce",status.abilities.pierce && direct>0 ? followup*0.25f : 0.0f},
                {"heavy_push",offer->definitionId=="player_heavy" ? prediction.breakBallHits*0.3f : 0.0f},
                {"anchor_position",status.abilities.anchor ? opportunity*0.5f : 0.0f},
				{"chain_impact",prediction.chainImpactHits*1.5f},
				// Actual prevented damage is already valued by safety; excess shield has no value.
				{"stop_shield",0.0f},
				{"refractive_pierce",status.abilities.refractAfterPierce && prediction.shot.playerEnemyContacts>1 ? followup*0.6f : 0.0f},
                {"bounce",offer->category==BallCategory::Bounce && prediction.shot.wallContacts>0 ? direct*0.1f : 0.0f},
                {"cushion_setup",offer->definitionId=="player_cushion_charge" ?
					CushionChargeRules::PendingNextShotCount(prediction.cushionCharges)*0.4f : 0.0f},
                {"power",-0.05f*aim.velocity.Length()}};
            float score = 0; for (const auto& value : breakdown) score += value.get<float>();
            scored.push_back({{"candidate_id",std::to_string(offerIndex)+":"+std::to_string(scored.size())},
                {"offer_index",offerIndex},{"instance_id",offer->instanceId},{"definition_id",offer->definitionId},
                {"target_id",aim.target},{"contact_kind",aim.kind},{"power",aim.velocity.Length()},
                {"direction",Position(Unit(aim.velocity))},{"velocity",Position(aim.velocity)},{"score",score},{"score_breakdown",breakdown},{"metrics",metrics},
                {"stop_position",Position(endPlayer->physics.position)},{"prediction_complete",true}});
        }
        std::stable_sort(scored.begin(),scored.end(),[](const json& a,const json& b) { return a["score"].get<float>()>b["score"].get<float>(); });
        if (!scored.empty()) offerChoices.push_back(scored[0]);
        // Retain selectable alternatives without publishing hundreds of near-identical shots.
        json reusable = json::array();
        for (std::size_t i=0;i<(std::min)(scored.size(),std::size_t{12});++i)
        { choices.push_back(scored[i]); reusable.push_back(scored[i]); }
        equivalentOffers.emplace(signature,std::move(reusable));
    }
    std::stable_sort(choices.begin(),choices.end(),[](const json& a,const json& b) { return a["score"].get<float>()>b["score"].get<float>(); });
    m_BossShotCacheKey = key;
    m_BossShotCache = {{"model","boss_shared_ccd_v4_escorts"},{"state_key",stateKey},{"deterministic",true},
        {"recommended",choices.empty()?json(nullptr):choices[0]},{"choices",choices},{"offer_choices",offerChoices},
        {"evaluated",evaluated},{"incomplete_rejected",incomplete},{"maximum_candidates_per_offer",MaximumCandidatesPerOffer},
        {"milliseconds",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()},
        {"limitations","One shot simulated exactly. Next-push opportunity and enemy-turn damage are estimates; respawn positions are not simulated. No aim jitter."}};
    return m_BossShotCache;
}

/// <summary>
/// 現在の盤面が評価時と一致することを確認して選択候補を発射し、判断の詳細を記録する。
/// </summary>
bool BossShotPlanner::Fire(Game& game, const std::string& candidateId, const std::string& stateKey)
{
    const auto evaluation = Evaluate(game);
    if (evaluation.empty() || evaluation["state_key"] != stateKey) return false;
    for (const auto& choice : evaluation["choices"])
    {
        if (choice["candidate_id"] != candidateId) continue;
        const auto& d = choice["velocity"];
        const Vector3 velocity(d["x"].get<float>(),0,d["z"].get<float>());
        const json telemetry = {
            {"boss_evaluation", choice},
            {"model", evaluation["model"]},
            {"state_key", stateKey},
        };
        if (!game.TryFireAutomatedShot(
            velocity,
            choice["offer_index"].get<int>(),
            &telemetry))
        {
            return false;
        }
        game.RecordBalanceEvent("boss_ai_decision", telemetry);
        return true;
    }
    return false;
}

/// <summary>
/// 現在のボス戦で可能なショットの候補を評価する。
/// </summary>
json Game::EvaluateBossShots()
{
    return m_BossShotPlanner->Evaluate(*this);
}

/// <summary>
/// 盤面の識別キーと候補IDを照合し、評価済みのボス戦用ショットを発射する。
/// </summary>
bool Game::FireBossPlannedShot(
    const std::string& candidateId,
    const std::string& stateKey)
{
    return m_BossShotPlanner->Fire(*this, candidateId, stateKey);
}

/// <summary>
/// デッキから提示されている球候補の件数を取得する。
/// </summary>
int Game::GetBallOfferCount() const
{
    return m_RunController->Deck().GetOfferCount();
}

/// <summary>
/// 指定位置の球候補のデータを取得する。
/// </summary>
const PlayerBallData* Game::GetBallOffer(int offerIndex) const
{
    return m_RunController->Deck().GetOffer(offerIndex);
}

/// <summary>
/// 現在のランで発射したショットの累計を取得する。
/// </summary>
std::uint64_t Game::GetTotalShotCount() const
{
    return static_cast<std::uint64_t>(
        m_PersistenceRuntime->Statistics().GetState().totalShots);
}

/// <summary>
/// 速度と盤面が発射条件を満たす場合に、指定の球と計測情報で自動ショットを発射する。
/// </summary>
bool Game::TryFireAutomatedShot(
    const Vector3& velocity,
    int offerIndex,
    const nlohmann::json* telemetry)
{
    if (!std::isfinite(velocity.x) ||
        !std::isfinite(velocity.y) ||
        !std::isfinite(velocity.z) ||
        velocity.LengthSquared() <= 0.0001f ||
        GetBattleState() != BattleState::AimingDirection ||
        !AreAllBallsStopped())
    {
        return false;
    }

    const auto players = GameRuntime::World().GetComponents<PlayerBall>();
    if (players.empty() || players[0] == nullptr || !players[0]->IsIdle())
    {
        return false;
    }

    if (offerIndex >= 0)
    {
        if (GetBallOffer(offerIndex) == nullptr)
        {
            return false;
        }
        ApplyBalanceAutoBallSelection(offerIndex);
    }

    *m_PendingShotTelemetry = telemetry != nullptr
        ? *telemetry
        : nlohmann::json::object();
    players[0]->FireAutomatedShot(velocity);
    return true;
}

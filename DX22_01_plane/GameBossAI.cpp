#include "Game.h"
#include "BallShotPrediction.h"
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
    Vector3 Flat(Vector3 v) { v.y = 0; return v; }
    Vector3 Unit(Vector3 v) { v = Flat(v); if (v.LengthSquared() > 0.00001f) v.Normalize(); return v; }
    json Position(const Vector3& v) { return {{"x",v.x},{"y",v.y},{"z",v.z}}; }

    // An explicitly geometric NEXT-shot opportunity, not a simulated second shot.
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

// Boss Shotsを評価する。
json Game::EvaluateBossShots()
{
    const auto players = GetComponents<PlayerBall>();
    EnemyBall* boss = nullptr;
    for (auto* enemy : GetComponents<EnemyBall>())
        if (enemy->IsArmorBoss() && !enemy->IsDefeated()) { boss = enemy; break; }
    if (!boss || players.empty() || !players[0]->IsIdle() || GetBattleState() != BattleState::AimingDirection || !AreAllBallsStopped()) return {};
    auto* player = players[0];
    std::string key = std::to_string(BallShotPrediction::WorldKey(*this)) + ":" + std::to_string(m_RunStatistics.GetState().totalShots);
    for (int i = 0; i < m_PlayerDeck.GetOfferCount(); ++i)
    {
        const auto* offer = m_PlayerDeck.GetOffer(i);
        if (offer) key += ":" + std::to_string(offer->instanceId) + offer->definitionId + WriteBallStatus(offer->status).dump();
    }
    // Expose an opaque compact key; comparison uses the complete key internally.
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : key) { hash ^= c; hash *= 1099511628211ull; }
    const std::string stateKey = std::to_string(hash);
    if (key == m_BossShotCacheKey && !m_BossShotCache.empty()) return m_BossShotCache;
    const auto started = std::chrono::steady_clock::now();
    const Vector3 start = player->GetPosition(), bossPosition = boss->GetPosition();
    std::vector<BreakBall*> neutrals;
    std::vector<Vector3> initialNeutrals;
    for (auto* neutral : GetComponents<BreakBall>())
        if (neutral->GetGameObject()->IsActive()) { neutrals.push_back(neutral); initialNeutrals.push_back(neutral->GetBall()->GetPosition()); }
    const float initialOpportunity = Opportunity(start,bossPosition,initialNeutrals);
    json choices = json::array(), offerChoices = json::array();
    std::map<std::string,json> equivalentOffers;
    int evaluated = 0, incomplete = 0;
    for (int offerIndex = 0; offerIndex < m_PlayerDeck.GetOfferCount(); ++offerIndex)
    {
        const auto* offer = m_PlayerDeck.GetOffer(offerIndex);
        if (!offer) continue;
        const std::string signature = offer->definitionId + WriteBallStatus(offer->status).dump();
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
        for (auto* neutral : neutrals)
        {
            const Vector3 n = neutral->GetBall()->GetPosition(), push = Unit(bossPosition-n);
            const std::string id = "break_ball:"+std::to_string(neutral->GetIndex());
            for (float power : {2.0f,4.0f,6.0f}) add(n,power,id,"neutral_center");
            for (float angle : {-0.3f,-0.15f,0.0f,0.15f,0.3f})
            {
                Vector3 direction(push.x*std::cos(angle)-push.z*std::sin(angle),0,push.x*std::sin(angle)+push.z*std::cos(angle));
                const Vector3 contact = n-direction*(status.radius+neutral->GetBall()->GetRadius());
                if (Unit(contact-start).Dot(direction) > 0.15f)
                    for (float power : {2.0f,4.0f,6.0f}) add(contact,power,id,"neutral_push");
            }
            const Vector3 setup = n-push*(status.radius+neutral->GetBall()->GetRadius()+7.0f);
            const Vector3 lateral(-push.z,0,push.x);
            for (float offset : {0.0f,-9.0f,9.0f})
            {
                const Vector3 destination = setup+lateral*offset;
                const float brake = status.friction*(status.abilities.anchor ? status.anchorBrakeMultiplier : 1.0f);
                const float power = std::sqrt(2.0f*brake*Flat(destination-start).Length()+0.03f);
                for (float scale : {0.8f,1.0f,1.2f}) add(destination,power*scale,id,"setup");
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
            const auto prediction = BallShotPrediction::Predict(*this,*player,aim.velocity,false,offer);
            if (!prediction.complete) { ++incomplete; continue; }
            const BallShotPrediction::Ball* endPlayer = nullptr;
            const BallShotPrediction::Ball* endBoss = nullptr;
            std::vector<Vector3> remainingNeutrals;
            for (const auto& ball : prediction.balls)
            {
                if (ball.physics.player) endPlayer = &ball;
                if (ball.physics.boss) endBoss = &ball;
                if (ball.physics.breakBall && ball.active) remainingNeutrals.push_back(ball.physics.position);
            }
            if (!endPlayer || !endBoss) continue;
            const int damage = boss->GetHP()-endBoss->hp;
            const int direct = (std::max)(0,damage-prediction.bossFixedDamage);
            const int armor = (std::max)(0,boss->GetBossState().armor-endBoss->bossState.armor);
            const bool startedBreak = !boss->GetBossState().IsBroken() && endBoss->bossState.IsBroken();
            const bool kill = endBoss->defeated;
            const float opportunity = !endPlayer->pocketed && !endPlayer->defeated ?
                Opportunity(endPlayer->physics.position,endBoss->physics.position,remainingNeutrals) : 0;
            const float hpLoss = static_cast<float>((std::max)(0,player->GetHP()-endPlayer->hp));
            float incoming = 0;
            for (const auto& ball : prediction.balls)
                if (ball.physics.enemy && !ball.defeated && !ball.pocketed) incoming += (std::max)(1,ball.attack-endPlayer->defense);
            const float urgency = player->GetHP() <= 10 ? 3.0f : 1.0f;
            const float followup = static_cast<float>((std::max)(0,prediction.shot.playerEnemyContacts-1));
            json metrics = {{"direct_damage",direct},{"fixed_damage",prediction.bossFixedDamage},
                {"armor_removed",armor},{"break_started",startedBreak},{"neutral_boss_hits",prediction.breakBallHits},
                {"next_push_opportunity_estimate",opportunity},{"opportunity_improvement",opportunity-initialOpportunity},
                {"player_hp_loss",hpLoss},{"expected_enemy_turn_damage",incoming},
                {"pierce_followup_contacts",status.abilities.pierce ? followup : 0},
                {"enemy_chain_contacts",prediction.shot.enemyEnemyContacts},{"wall_contacts",prediction.shot.wallContacts},
                {"anchor_stopped",prediction.shot.anchorStopped},{"boss_killed",kill}};
            json breakdown = {
                {"direct_damage",direct*(boss->GetBossState().IsBroken() ? 1.25f : 1.0f)},
                {"fixed_damage",prediction.bossFixedDamage*1.0f},{"armor",armor*3.0f},
                {"start_break",startedBreak ? 6.0f : 0.0f},{"kill",kill ? 100.0f : 0.0f},
                {"setup",(opportunity-initialOpportunity)*5.0f+opportunity},
                {"safety",-(hpLoss*4.0f+incoming)*urgency},
                {"fatal",endPlayer->defeated || (!kill && endPlayer->hp <= incoming) ? -1000.0f : 0.0f},
                {"wasted_break",boss->GetBossState().IsBroken() && damage==0 ? -6.0f : 0.0f},
                {"pierce",status.abilities.pierce && direct>0 ? followup*0.25f : 0.0f},
                {"heavy_push",offer->definitionId=="player_heavy" ? prediction.breakBallHits*0.3f : 0.0f},
                {"anchor_position",status.abilities.anchor ? opportunity*0.5f : 0.0f},
                {"bounce",offer->definitionId=="player_bounce" && prediction.shot.wallContacts>0 ? direct*0.1f : 0.0f},
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
    m_BossShotCache = {{"model","boss_shared_ccd_v1"},{"state_key",stateKey},{"deterministic",true},
        {"recommended",choices.empty()?json(nullptr):choices[0]},{"choices",choices},{"offer_choices",offerChoices},
        {"evaluated",evaluated},{"incomplete_rejected",incomplete},{"maximum_candidates_per_offer",MaximumCandidatesPerOffer},
        {"milliseconds",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()},
        {"limitations","One shot simulated exactly. Next-push opportunity and enemy-turn damage are estimates; respawn positions are not simulated. No aim jitter."}};
    return m_BossShotCache;
}

// Boss Planned Shotを発射する。
bool Game::FireBossPlannedShot(const std::string& candidateId, const std::string& stateKey)
{
    const auto evaluation = EvaluateBossShots();
    if (evaluation.empty() || evaluation["state_key"] != stateKey) return false;
    for (const auto& choice : evaluation["choices"])
    {
        if (choice["candidate_id"] != candidateId) continue;
        const auto players = GetComponents<PlayerBall>();
        if (players.empty()) return false;
        m_SelectedOfferIndex = choice["offer_index"].get<int>();
        if (m_SelectedHoldIndex == m_SelectedOfferIndex) m_SelectedHoldIndex = -1;
        ApplySelectedBallPreview();
        const auto& d = choice["velocity"];
        const Vector3 velocity(d["x"].get<float>(),0,d["z"].get<float>());
        m_PendingShotTelemetry = {{"boss_evaluation",choice},{"model",evaluation["model"]},{"state_key",stateKey}};
        RecordBalanceEvent("boss_ai_decision",m_PendingShotTelemetry);
        players[0]->FireAutomatedShot(velocity);
        return true;
    }
    return false;
}

from pathlib import Path
P=Path(__file__).resolve().parents[1]
def edit(name,changes):
 p=P/name; raw=p.read_bytes(); enc='utf-8-sig' if raw.startswith(b'\xef\xbb\xbf') else 'utf-8';s=raw.decode(enc).replace('\r\n','\n')
 for a,b in changes:
  assert a in s,(name,a[:80]);s=s.replace(a,b,1)
 p.write_bytes(s.replace('\n','\r\n').encode(enc))
edit('BallShotPrediction.h', [('class PlayerBall;', 'class PlayerBall;\nstruct PlayerBallData;'),
 ('        int ticks = 0, substeps = 0, damage = 0;', '        int ticks = 0, substeps = 0, damage = 0;\n        int bossFixedDamage = 0, breakBallHits = 0;'),
 ('const DirectX::SimpleMath::Vector3& velocity, bool skipFirstPlayerFriction);', 'const DirectX::SimpleMath::Vector3& velocity, bool skipFirstPlayerFriction, const PlayerBallData* offer = nullptr);')])
edit('BallShotPrediction.cpp', [('#include "PlayerBall.h"', '#include "PlayerBall.h"\n#include "PlayerBallData.h"'),
 ('bool skipFirstPlayerFriction)\n{', 'bool skipFirstPlayerFriction, const PlayerBallData* offer)\n{'),
 ('    result.shot = game.MakePredictionShotRules(velocity.Length());', '    result.shot = game.MakePredictionShotRules(velocity.Length());\n    if (offer) result.shot.ballId = offer->definitionId;'),
 ('        ball.attack = ball.physics.status.attack + game.GetRelicAttackBonus();', '''        if (offer)
        {
            ball.physics.status = offer->status;
            ball.defense = offer->status.defense + game.GetRelicDefenseBonus();
            ball.physics.pierceLimit = offer->status.pierceMaxUses;
            ball.physics.pierceRetention = offer->status.pierceSpeedRetention;
            if (offer->definitionId == "player_pierce" && game.HasRelic(RelicType::PierceBallCharger))
            { ++ball.physics.pierceLimit; ball.physics.pierceRetention = 1.0f; }
        }
        ball.attack = ball.physics.status.attack + game.GetRelicAttackBonus();'''),
 ('                        result.damage += before - boss.hp;', '                        result.damage += before - boss.hp;\n                        result.bossFixedDamage += before - boss.hp;\n                        ++result.breakBallHits;')])
edit('Game.h', [('\tPlayerDeck m_PlayerDeck;', '\tPlayerDeck m_PlayerDeck;\n    nlohmann::json m_BossShotCache;\n    std::string m_BossShotCacheKey;'),
 ('\tbool WasLastRunCompleted() const', '''    nlohmann::json EvaluateBossShots();
    bool FireBossPlannedShot(const std::string& candidateId, const std::string& stateKey);
\tbool WasLastRunCompleted() const''')])
edit('GameAutoPlay.cpp', [('void Game::SelectBalanceAutoBall()\n{', '''void Game::SelectBalanceAutoBall()
{
    const auto bossChoices = EvaluateBossShots();
    if (bossChoices.contains("recommended") && !bossChoices["recommended"].is_null())
    {
        m_SelectedOfferIndex = bossChoices["recommended"]["offer_index"].get<int>();
        if (m_SelectedHoldIndex == m_SelectedOfferIndex) m_SelectedHoldIndex = -1;
        ApplySelectedBallPreview();
        return;
    }'''),
 ('bool Game::FireBalanceAutoShot()\n{', '''bool Game::FireBalanceAutoShot()
{
    const auto bossChoices = EvaluateBossShots();
    if (bossChoices.contains("recommended") && !bossChoices["recommended"].is_null())
        return FireBossPlannedShot(bossChoices["recommended"]["candidate_id"].get<std::string>(),
            bossChoices["state_key"].get<std::string>());''')])
edit('DX22_01_plane.vcxproj', [('    <ClCompile Include="GameAutoPlay.cpp" />','    <ClCompile Include="GameAutoPlay.cpp" />\n    <ClCompile Include="GameBossAI.cpp"><Optimization>MaxSpeed</Optimization><BasicRuntimeChecks>Default</BasicRuntimeChecks></ClCompile>')])
edit('GameMcpBridge.cpp', [
 ('\t\tstate["available_actions"].push_back(\n\t\t\t"fire_shot");', '''\t\tstate["available_actions"].push_back(
\t\t\t"fire_shot");
        if (!state["boss_state"].is_null())
        {
            state["available_actions"].push_back("evaluate_boss_shots");
            state["available_actions"].push_back("fire_boss_shot");
        }'''),
 ('\tif (action == "set_dynamic_balance")', '''    if (action == "evaluate_boss_shots" || action == "fire_boss_shot")
    {
        if (scene != "battle" || game.GetGameState() != GameState::AimingDirection || !game.AreAllBallsStopped())
            return CommandResult(false, "Boss planning requires a stopped battle in aiming state.");
        const auto choices = game.EvaluateBossShots();
        if (choices.empty()) return CommandResult(false, "No live Armor boss or playable ball.");
        if (action == "evaluate_boss_shots")
        {
            auto result = CommandResult(true, "Evaluated with shared CCD/TOI prediction; gameplay unchanged.");
            result["evaluation"] = choices;
            return result;
        }
        const bool fired = game.FireBossPlannedShot(arguments.value("candidate_id", std::string()),
            arguments.value("state_key", std::string()));
        return CommandResult(fired, fired ? "Fired the selected boss plan." : "Stale or invalid plan. Evaluate again.");
    }
\tif (action == "set_dynamic_balance")''')])
print('Boss planner integration points added')

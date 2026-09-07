from pathlib import Path
P = Path(__file__).resolve().parents[1]
def edit(name, changes):
    p=P/name; raw=p.read_bytes(); codec='utf-8-sig' if raw.startswith(b'\xef\xbb\xbf') else 'utf-8'
    s=raw.decode(codec).replace('\r\n','\n')
    for old,new in changes:
        assert old in s,(name,old[:100]); s=s.replace(old,new,1)
    p.write_bytes(s.replace('\n','\r\n').encode(codec))
edit('BallShotPrediction.h', [('#include "ShotRelicRules.h"','#include "ShotRelicRules.h"\n#include "BossCombatRules.h"'),
    ('        int stopCount = 0;', '        int stopCount = 0;\n        BossCombatRules::State bossState;\n        bool breakBallUsed = false;')])
edit('BallShotPrediction.cpp', [('#include "EnemyBall.h"', '#include "EnemyBall.h"\n#include "BreakBall.h"'),
    ('                ball.physics.enemy = true;', '                ball.physics.enemy = true;\n                ball.physics.boss = enemy->IsArmorBoss();\n                ball.bossState = enemy->GetBossState();'),
    ('            ball.active = collision->CanSimulate();', '''            if (const auto* neutral = owner->GetComponent<BreakBall>())
            {
                ball.physics.breakBall = true;
                ball.pocketed = neutral->IsPocketed();
                ball.breakBallUsed = neutral->IsUsed();
            }
            ball.active = collision->CanSimulate();'''),
    ('        bool CheckPocket(Ball& ball, const Vector3& start)\n        {', '''        bool CheckPocket(Ball& ball, const Vector3& start)
        {
            if (ball.physics.boss) return false;'''),
    ('                ball.active = false;', '''                if (ball.physics.breakBall)
                {
                    ball.pocketed = true;
                    ball.breakBallUsed = false;
                    ball.physics.position = Vector3(0, -1000, 0);
                }
                ball.active = false;'''),
    ('            for (const auto& wall : walls)\n            {', '''            if (ball.physics.boss)
                for (const auto& wall : BallPhysicsRules::BossWalls())
                    BallPhysicsRules::Wall(ball.physics, wall, Vector3::Zero);
            for (const auto& wall : walls)
            {'''),
    ('            target.hp = (std::max)(0, target.hp - (std::max)(1, amount - target.defense));', '''            const int applied = target.physics.boss ? BossCombatRules::DirectDamage(amount, target.defense, target.bossState) :
                (std::max)(1, amount - target.defense);
            target.hp = (std::max)(0, target.hp - applied);'''),
    ('                const bool playerEnemy =', '''                if (a.physics.breakBall || b.physics.breakBall)
                {
                    auto hit = [&](Ball& neutral, Ball& boss) {
                        if (!neutral.physics.breakBall || !boss.physics.boss || boss.defeated) return;
                        boss.bossState.HitBreakBall();
                        const int before = boss.hp;
                        boss.hp = (std::max)(0, before - BossCombatRules::BreakBallDamage);
                        boss.defeated = boss.hp == 0;
                        result.damage += before - boss.hp;
                        neutral.active = false;
                        neutral.breakBallUsed = true;
                        neutral.pocketed = false;
                        neutral.physics.position = Vector3(0, -1000, 0);
                        neutral.physics.velocity = neutral.acceleration = Vector3::Zero;
                    };
                    hit(a, b); hit(b, a);
                    if (BallPhysicsRules::StopAnchor(a.physics, a.acceleration)) result.shot.Anchor();
                    if (BallPhysicsRules::StopAnchor(b.physics, b.acceleration)) result.shot.Anchor();
                }
                else
                {
                const bool playerEnemy ='''),
    ('                if (playerEnemy || enemyEnemy) result.shot.Contact(playerEnemy);', '                if (playerEnemy || enemyEnemy) result.shot.Contact(playerEnemy);\n                }'),
    ('{"hp", ball.hp}, {"active", ball.active}, {"defeated", ball.defeated}, {"pocketed", ball.pocketed}};', '''{"hp", ball.hp}, {"active", ball.active}, {"defeated", ball.defeated}, {"pocketed", ball.pocketed},
            {"boss", ball.physics.boss}, {"break_ball", ball.physics.breakBall}, {"break_ball_used", ball.breakBallUsed},
            {"armor", ball.bossState.armor}, {"break_shots_remaining", ball.bossState.shotsRemaining},
            {"break_started_this_shot", ball.bossState.startedThisShot}};'''),
    ('        if (ball.physics.id != world.playerId) continue;', '        if (ball.physics.boss) ball.bossState.BeginShot();\n        if (ball.physics.id != world.playerId) continue;'),
    ('            if (ball.physics.enemy) BallPhysicsRules::EnemyFriction', '            if (ball.physics.enemy || ball.physics.breakBall) BallPhysicsRules::EnemyFriction'),
    ('        add(ball.defeated); add(ball.pocketed);', '''        add(ball.defeated); add(ball.pocketed);
        add(ball.physics.boss); add(ball.physics.breakBall); add(ball.breakBallUsed);
        add(ball.bossState.armor); add(ball.bossState.shotsRemaining); add(ball.bossState.startedThisShot);''')])
edit('GamePresentation.cpp', [('#include "EnemyBall.h"', '#include "EnemyBall.h"\n#include "BreakBall.h"'),
    ('\t\t\t\tif (enemy->GetFrontalDamageMultiplier() < 1.0f)', '''                if (enemy->IsArmorBoss())
                {
                    const auto& boss = enemy->GetBossState();
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.6f, 1.0f), "最終ボス  HP %d / %d", enemy->GetHP(), enemy->GetMaxHP());
                    ImGui::Text("Armor %d / %d", boss.armor, BossCombatRules::MaxArmor);
                    if (boss.IsBroken())
                    {
                        ImGui::TextColored(ImVec4(1, 0.85f, 0.2f, 1), "BREAK！ 通常ダメージ100%% / 残り%dショット", boss.shotsRemaining);
                        if (boss.startedThisShot && game.GetGameState() == GameState::BallsMoving)
                            ImGui::TextUnformatted("このショットの残り ＋ 次の2ショットが有効");
                    }
                    else ImGui::TextUnformatted("通常ダメージ25%（切り上げ・最低1） / ポケット無効");
                    ImGui::TextUnformatted("黄色の球をボスへ押し込もう：Armor -1 / HP -4");
                    ImGui::TextUnformatted("黄色の球は命中・落下後、ショット終了時に再配置");
                    for (auto* neutral : game.GetComponents<BreakBall>())
                        ImGui::Text("ブレイク球%d：%s", neutral->GetIndex() + 1,
                            neutral->GetGameObject()->IsActive() ? "使用可能" : "再配置待ち");
                }
\t\t\t\tif (enemy->GetFrontalDamageMultiplier() < 1.0f)''')])
edit('GameMcpBridge.cpp', [('#include "EnemyBall.h"', '#include "EnemyBall.h"\n#include "BreakBall.h"'),
    ('\tstate["enemies"] = nlohmann::json::array();', '''    state["boss_state"] = nullptr;
    state["break_balls"] = nlohmann::json::array();
    for (auto* neutral : game.GetComponents<BreakBall>())
    {
        auto* ball = neutral->GetBall();
        state["break_balls"].push_back({
            {"target_id", "break_ball:" + std::to_string(neutral->GetIndex())},
            {"position", VectorToJson(ball->GetPosition())}, {"velocity", VectorToJson(ball->GetVelocity())},
            {"radius", ball->GetRadius()}, {"mass", ball->GetStatus().mass},
            {"active", neutral->GetGameObject()->IsActive()}, {"used_this_shot", neutral->IsUsed()},
            {"pocketed", neutral->IsPocketed()}, {"fixed_boss_damage", BossCombatRules::BreakBallDamage},
            {"armor_damage", 1}, {"max_activations_per_shot", 1}, {"pierce_passes_through", false},
            {"consumes_enemy_damage_relics", false}});
    }
\tstate["enemies"] = nlohmann::json::array();'''),
    ('\t\tstate["enemies"].push_back({', '''        if (enemy->IsArmorBoss())
        {
            const auto& boss = enemy->GetBossState();
            state["boss_state"] = {
                {"boss_id", enemy->GetEnemyId()}, {"target_id", "enemy:" + std::to_string(enemyIndex)},
                {"phase", 1}, {"hp", enemy->GetHP()}, {"max_hp", enemy->GetMaxHP()},
                {"armor", boss.armor}, {"max_armor", BossCombatRules::MaxArmor},
                {"is_broken", boss.IsBroken()}, {"break_shots_remaining", boss.shotsRemaining},
                {"break_started_this_shot", boss.startedThisShot}, {"pocket_immune", true},
                {"direct_damage_multiplier", boss.IsBroken() ? 1.0f : 0.25f},
                {"damage_order", "directional_then_defense_then_armor_ceil_min1"},
                {"break_ball_refreshes_break", false}, {"trigger_shot_consumes_break", false}};
        }
\t\tstate["enemies"].push_back({'''),
    ('\t\t\t{ "pocketed", enemy->IsPocketed() },', '\t\t\t{ "pocketed", enemy->IsPocketed() },\n            { "pocket_immune", enemy->IsArmorBoss() },')])
print('Prediction, HUD, MCP state integrated')

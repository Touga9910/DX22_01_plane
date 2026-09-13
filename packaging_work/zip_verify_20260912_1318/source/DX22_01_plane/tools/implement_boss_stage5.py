"""One-shot, asserted edits to the archived stage-4 sources."""
from pathlib import Path
import json
P = Path(__file__).resolve().parents[1]

def edit(name, changes):
    p = P / name
    raw = p.read_bytes()
    codec = 'utf-8-sig' if raw.startswith(b'\xef\xbb\xbf') else 'utf-8'
    s = raw.decode(codec).replace('\r\n', '\n')
    for old, new in changes:
        assert old in s, (name, old[:100])
        s = s.replace(old, new, 1)
    p.write_bytes(s.replace('\n','\r\n').encode(codec))

edit('EnemyBall.h', [
    ('#include "EnemyData.h"', '#include "EnemyData.h"\n#include "BossCombatRules.h"'),
    ('    void TakeDamage(int damage);', '''    void TakeDamage(int damage);
    bool IsArmorBoss() const { return m_EnemyData.id == "enemy_boss_core"; }
    const BossCombatRules::State& GetBossState() const { return m_BossState; }
    void BeginBossShot() { if (IsArmorBoss()) m_BossState.BeginShot(); }
    void EndBossShot();
    void HitBreakBall(int ballId);'''),
    ('    bool m_IsPocketed = false;', '    bool m_IsPocketed = false;\n    BossCombatRules::State m_BossState;')])
edit('EnemyBall.cpp', [
    ('    m_EnemyData = data;', '    m_BossState = {};\n    m_EnemyData = data;'),
    ('    Game::GetInstance()->HandleEnemyPocket(this);', '    if (!IsArmorBoss()) Game::GetInstance()->HandleEnemyPocket(this);'),
    ('\tm_Ball->TakeDamage(damage);', '''    if (IsArmorBoss()) damage = BossCombatRules::DirectDamage(damage, GetDefense(), m_BossState) + GetDefense();
\tm_Ball->TakeDamage(damage);'''),
    ('    if (IsDefeated() || m_EnemyData.pocketDamageRatio <= 0.0f)', '    if (IsArmorBoss() || IsDefeated() || m_EnemyData.pocketDamageRatio <= 0.0f)')])
with (P/'EnemyBall.cpp').open('a',encoding='utf-8',newline='') as f:
    f.write('''
void EnemyBall::HitBreakBall(int ballId)
{
    if (!IsArmorBoss() || IsDefeated()) return;
    const int hpBefore = GetHP(), armorBefore = m_BossState.armor;
    const bool started = m_BossState.HitBreakBall();
    const Vector3 velocity = GetVelocity();
    SetHP((std::max)(0, hpBefore - BossCombatRules::BreakBallDamage));
    if (GetHP() == 0)
    {
        Defeat();
        m_Ball->GetMutableVelocity() = velocity;
        Game::GetInstance()->NotifyEnemyDefeated(GetEnemyId());
    }
    const int damage = hpBefore - GetHP();
    auto& logger = BalanceLogger::GetInstance();
    logger.RecordEnemyDamage(GetEnemyId(), damage);
    logger.RecordEvent("boss_break_ball_hit", {{"ball_id", ballId}, {"boss_id", GetEnemyId()},
        {"armor_before", armorBefore}, {"armor_after", m_BossState.armor},
        {"boss_damage", damage}, {"hp_before", hpBefore}, {"hp_after", GetHP()}});
    if (started) logger.RecordEvent("boss_break_started", {{"boss_id", GetEnemyId()},
        {"break_shots_remaining", m_BossState.shotsRemaining}, {"trigger_shot_excluded", true}});
    Game::GetInstance()->NotifyCombatFeedback(GetPosition(), damage, IsDefeated(), false);
}

void EnemyBall::EndBossShot()
{
    if (IsArmorBoss() && !IsDefeated() && m_BossState.EndShot())
        BalanceLogger::GetInstance().RecordEvent("boss_break_ended",
            {{"boss_id", GetEnemyId()}, {"armor", m_BossState.armor}});
}
'''.replace('\n','\r\n'))
edit('BallFactory.h', [('class EnemyBall;', 'class EnemyBall;\nclass BreakBall;'),
    ('    static PlayerBall* CreatePlayer(Game& game);', '    static PlayerBall* CreatePlayer(Game& game);\n    static BreakBall* CreateBreakBall(Game& game, int index);')])
edit('BallFactory.cpp', [('#include "BallFactory.h"', '#include "BallFactory.h"\n#include "BreakBall.h"'),
    ('PlayerBall* BallFactory::CreatePlayer(Game& game)', '''BreakBall* BallFactory::CreateBreakBall(Game& game, int index)
{
    auto* object = CreateBallObject(game, "BreakBall", GameObjectTag::None);
    return object->AddComponent<BreakBall>(index);
}

PlayerBall* BallFactory::CreatePlayer(Game& game)''')])
edit('BattleScene.cpp', [('#include "BallFactory.h"', '#include "BallFactory.h"\n#include "BreakBall.h"'),
    ('\t\tGame::GetInstance()->OnBattleStageStarted(adjustedStage);', '''\t\tGame::GetInstance()->OnBattleStageStarted(adjustedStage);
        const bool hasArmorBoss = std::any_of(adjustedStage.enemies.begin(), adjustedStage.enemies.end(),
            [](const EnemySpawnData& spawn) { return spawn.enemyData.id == "enemy_boss_core"; });
        if (hasArmorBoss)
        {
            for (int index = 0; index < 2; ++index)
            {
                auto* neutral = BallFactory::CreateBreakBall(*game, index);
                m_SceneGameObjects.emplace_back(neutral->GetGameObject());
                neutral->Reposition();
            }
        }''')])
edit('GameProgression.cpp', [('#include "EnemyBall.h"', '#include "EnemyBall.h"'),
    ('    for (auto* ball : GetComponents<BallComponent>()) ball->ResetShotAbilityState();', '''    for (auto* ball : GetComponents<BallComponent>()) ball->ResetShotAbilityState();
    for (auto* enemy : GetComponents<EnemyBall>()) enemy->BeginBossShot();''')])
edit('Game.cpp', [('#include "EnemyBall.h"', '#include "EnemyBall.h"\n#include "BreakBall.h"'),
    ('\t\t\tm_Instance->FinishDynamicBalanceShot();', '''\t\t\tm_Instance->FinishDynamicBalanceShot();
            for (auto* enemy : enemies) enemy->EndBossShot();
            if (!m_Instance->AreAllEnemiesDefeated())
                for (auto* neutral : m_Instance->GetComponents<BreakBall>()) neutral->Reposition();'''),
    ('bool Game::IsEnemyPocketFinisherEligible(const EnemyBall* enemy) const\n{', '''bool Game::IsEnemyPocketFinisherEligible(const EnemyBall* enemy) const
{
    if (enemy && enemy->IsArmorBoss()) return false;'''),
    ('void Game::HandleEnemyPocket(EnemyBall* enemy)\n{', '''void Game::HandleEnemyPocket(EnemyBall* enemy)
{
    if (enemy && enemy->IsArmorBoss()) return;''')])

edit('BallPhysicsRules.h', [('#include "BallCcdGeometry.h"', '#include "BallCcdGeometry.h"\n#include "TableConfig.h"'),
    ('        bool player = false, enemy = false;', '        bool player = false, enemy = false, breakBall = false, boss = false;'),
    ('    inline bool PlayerFriction(', '''    // Closed rectangle only for the boss; ordinary balls retain pocket openings.
    inline auto BossWalls()
    {
        const float x = TableConfig::GetFieldWidth() * 0.5f;
        const float z = TableConfig::GetFieldDepth() * 0.5f;
        const float y = TableConfig::FIELD_HEIGHT;
        return std::array<Collision::Segment, 4>{{
            {{-x,y,-z},{x,y,-z}}, {{x,y,-z},{x,y,z}},
            {{x,y,z},{-x,y,z}}, {{-x,y,z},{-x,y,-z}}
        }};
    }

    inline bool PlayerFriction('''),
    ('a.player && b.enemy ? a.status.knockbackTransfer', 'a.player && (b.enemy || b.breakBall) ? a.status.knockbackTransfer'),
    ('b.player && a.enemy ? b.status.knockbackTransfer', 'b.player && (a.enemy || a.breakBall) ? b.status.knockbackTransfer'),
    ('if (a.player && b.enemy && a.status.abilities.anchor)', 'if (a.player && (b.enemy || b.breakBall) && a.status.abilities.anchor)'),
    ('if (b.player && a.enemy && b.status.abilities.anchor)', 'if (b.player && (a.enemy || a.breakBall) && b.status.abilities.anchor)')])
edit('ContinuousBallStepper.h', [
    ('                for (const auto& pocket : world.PocketSpheres())', '                if (!a.boss) for (const auto& pocket : world.PocketSpheres())'),
    ('                for (const auto& wall : world.Walls())\n                    earliest', '''                if (a.boss) for (const auto& wall : BallPhysicsRules::BossWalls())
                    earliest = (std::min)(earliest, BallCcdGeometry::WallTime(a.position, a.velocity,
                        a.status.radius, wall, remaining));
                for (const auto& wall : world.Walls())
                    earliest''')])
edit('BallCollisionComponent.cpp', [('#include "EnemyBall.h"', '#include "EnemyBall.h"\n#include "BreakBall.h"'),
    ('    auto body = CapturePhysicsBody();\n    for (const auto& wall : walls)', '''    auto body = CapturePhysicsBody();
    if (body.boss)
    {
        for (const auto& wall : BallPhysicsRules::BossWalls()) BallPhysicsRules::Wall(body, wall, Vector3::Zero);
        CommitPhysicsBody(body);
    }
    for (const auto& wall : walls)'''),
    ('    const bool isPlayerEnemyCollision =', '''    // Neutral contacts are physical only, except a one-shot fixed hit on the boss.
    // Return before ContactDamage, minimum damage, and damage-relic consumption.
    if (first.breakBall || second.breakBall)
    {
        if (first.breakBall && otherEnemy) GetGameObject()->GetComponent<BreakBall>()->HitBoss(*otherEnemy);
        if (second.breakBall && myEnemy) other->GetGameObject()->GetComponent<BreakBall>()->HitBoss(*myEnemy);
        if (BallPhysicsRules::StopAnchor(first, m_PhysicsComponent->Acceleration()))
        {
            CommitPhysicsBody(first);
            Game::GetInstance()->NotifyAnchorStopped();
        }
        if (BallPhysicsRules::StopAnchor(second, otherCollision->m_PhysicsComponent->Acceleration()))
        {
            otherContact.CommitPhysicsBody(second);
            Game::GetInstance()->NotifyAnchorStopped();
        }
        return;
    }
    const bool isPlayerEnemyCollision ='''),
    ('\tif (m_BallComponent == nullptr)\n\t{\n\t\treturn false;\n\t}', '''    if (const auto* enemy = GetGameObject()->GetComponent<EnemyBall>(); enemy && enemy->IsArmorBoss()) return false;
\tif (m_BallComponent == nullptr)
\t{
\t\treturn false;
\t}'''),
    ('    body.enemy = GetGameObject()->GetComponent<EnemyBall>() != nullptr;', '''    const auto* enemy = GetGameObject()->GetComponent<EnemyBall>();
    body.enemy = enemy != nullptr;
    body.boss = enemy && enemy->IsArmorBoss();
    body.breakBall = GetGameObject()->GetComponent<BreakBall>() != nullptr;''')])

edit('DX22_01_plane.vcxproj', [('    <ClInclude Include="BallShotPrediction.h" />', '''    <ClInclude Include="BallShotPrediction.h" />
    <ClInclude Include="BossCombatRules.h" />
    <ClInclude Include="BreakBall.h" />
    <ClCompile Include="BreakBall.cpp" />''')])
data = P/'assets/data'
enemies = json.loads((data/'enemy_data.json').read_text(encoding='utf-8-sig'))
for enemy in enemies['enemies']:
    if enemy['id'] == 'enemy_boss_core':
        enemy['status'].update(maxHp=60, mass=18.0, radius=6.0)
        enemy['scale'] = dict(x=6.0,y=6.0,z=6.0)
(data/'enemy_data.json').write_text(json.dumps(enemies,ensure_ascii=False,indent=2)+'\n',encoding='utf-8-sig')
stages = json.loads((data/'stage_01.json').read_text(encoding='utf-8-sig'))
for stage in stages['stages']:
    if stage['stageType'] == 'boss':
        stage['enemies'] = [dict(enemyId='enemy_boss_core',position=[0.0,1.0,20.0])]
(data/'stage_01.json').write_text(json.dumps(stages,ensure_ascii=False,indent=2)+'\n',encoding='utf-8-sig')
print('Live boss and neutral-ball integration applied')

#include "BallShotPrediction.h"
#include "PlayerShieldRules.h"
#include "AnchorStackRules.h"
#include "BallCollisionComponent.h"
#include "BallComponent.h"
#include "BallPhysicsComponent.h"
#include "BallPhysicsWorld.h"
#include "EnemyBall.h"
#include "EnemyGimmickRules.h"
#include "BreakBall.h"
#include "NuisanceBall.h"
#include "PierceMinion.h"
#include "json/json.hpp"
#include "GameObject.h"
#include "PlayerBall.h"
#include "PlayerBallData.h"
#include "Pocket.h"
#include "BossPocketRules.h"
#include "TableFrame.h"

#include <bit>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numeric>

using namespace DirectX::SimpleMath;
using namespace BallShotPrediction;

namespace
{
    constexpr int MaxTicks = 1200;
    constexpr int MaxTotalSubsteps = 60000;
    constexpr std::size_t MaxPathPoints = 1000;

    /// <summary>
    /// 各ボールの物理・戦闘状態をコピーし、ショット予測用の初期状態を組み立てる。
    /// </summary>
    std::vector<Ball> Capture(const Input& input)
    {
        std::vector<Ball> result;
        for (auto* component : input.balls)
        {
            if (!component || !component->GetGameObject()) continue;
            auto* owner = component->GetGameObject();
            auto* collision = owner->GetComponent<BallCollisionComponent>();
            if (!collision) continue;
            // Binding normally happens in the real world's fixed update. Reading a
            // snapshot must not repair/change component state; initialization binds it.
            Ball ball;
            ball.physics.id = reinterpret_cast<std::uintptr_t>(component);
            ball.physics.position = component->GetPosition();
            ball.physics.velocity = component->GetVelocity();
            ball.physics.status = component->GetStatus();
            if (const auto* physical = owner->GetComponent<BallPhysicsComponent>())
            {
                ball.physics.status.radius = physical->GetRadius();
                ball.physics.status.mass = physical->GetMass();
                ball.physics.status.restitution = physical->GetRestitution();
                ball.physics.status.friction = physical->GetFriction();
                ball.acceleration = physical->GetAcceleration();
            }
            ball.hp = component->GetHP();
            ball.maxHp = component->GetMaxHP();
            ball.attack = component->GetAttack();
            ball.defense = component->GetDefense();
            ball.defeated = component->IsDefeated();
            if (const auto* player = owner->GetComponent<PlayerBall>())
            {
                ball.physics.player = true;
				ball.defense = 0;
                ball.pocketed = player->IsPocketed();
            }
            if (const auto* enemy = owner->GetComponent<EnemyBall>())
            {
                // Match the runtime solver: defeated balls remain active
                // colliders, but do not participate in enemy-only abilities
                // such as pierce bypass and its forced exit motion.
                ball.physics.enemy = enemy->CanParticipateInCombat();
                ball.physics.boss = enemy->IsArmorBoss();
                ball.bossState = enemy->GetBossState();
                ball.bossTurn = enemy->BossTurn();
                ball.bossMinion = enemy->IsBossMinion();
                ball.plannedAttackDamage = enemy->CanParticipateInCombat() && !enemy->IsStunned() &&
                    (enemy->GetPlannedAction()==EnemyActionType::NormalAttack || enemy->GetPlannedAction()==EnemyActionType::ChargeAttack)
                    ? static_cast<int>(std::lround(enemy->GetAttack()*enemy->GetPlannedDamageMultiplier())) : 0;
                ball.enemyId = enemy->GetEnemyId();
                ball.pocketed = enemy->IsPocketed();
                ball.frontalMultiplier = enemy->GetFrontalDamageMultiplier();
                ball.pocketDamageRatio = enemy->GetPocketDamageRatio();
                ball.collisionDamageMultipliers = enemy->GetCollisionDamageMultipliers();
                ball.collisionStage = enemy->GetCollisionStage();
                ball.collisionGraceTicks = enemy->GetCollisionCountGraceTicks();
				ball.anchorStacks = enemy->GetAnchorStacks();
				ball.physics.nextKnockbackMultiplier =
					enemy->GetNextKnockbackMultiplier();
            }
            if (const auto* neutral = owner->GetComponent<BreakBall>())
            {
                ball.physics.breakBall = true;
                ball.pocketed = neutral->IsPocketed();
                ball.breakBallUsed = neutral->IsUsed();
            }
			ball.physics.minion = owner->GetComponent<PierceMinion>() != nullptr;
            ball.nuisanceBall = owner->GetComponent<NuisanceBall>() != nullptr;
            ball.active = collision->CanSimulate();
            result.push_back(std::move(ball));
        }
        return result;
    }

    struct World
    {
        Result& result;
        std::vector<std::size_t> order;
        std::vector<Vector3> starts;
        std::vector<Collision::Segment> walls;
        std::vector<Collision::Sphere> pockets;
        Vector3 interior = Vector3::Zero, lastPathVelocity = Vector3::Zero;
        std::uintptr_t playerId = 0;
        int playerPocketDamage = 1;
        bool gameOver = false;
        /// <summary>
        /// ショット予測で使うポケットの球形領域一覧を返す。
        /// </summary>
        const auto& PocketSpheres() const { return pockets; }
        /// <summary>
        /// ショット予測で使う壁の線分一覧を返す。
        /// </summary>
        const auto& Walls() const { return walls; }
        /// <summary>
        /// 指定番号のボールの予測物理状態を読み取り専用で返す。
        /// </summary>
        const auto& PhysicsBody(std::size_t i) const { return At(i).physics; }

        /// <summary>
        /// 処理順の番号から、更新可能な予測ボールを取得する。
        /// </summary>
        Ball& At(std::size_t i) { return result.balls[order[i]]; }
        /// <summary>
        /// 処理順の番号から、読み取り専用の予測ボールを取得する。
        /// </summary>
        const Ball& At(std::size_t i) const { return result.balls[order[i]]; }
        /// <summary>
        /// 今回の予測で処理するボールの個数を返す。
        /// </summary>
        std::size_t Count() const { return order.size(); }
        /// <summary>
        /// 指定番号のボールが予測内で活動中かを返す。
        /// </summary>
        bool IsActive(std::size_t i) const { return At(i).active; }
        /// <summary>
        /// 指定番号のボールの予測上の衝突半径を返す。
        /// </summary>
        float Radius(std::size_t i) const { return At(i).physics.status.radius; }
        /// <summary>
        /// 指定番号のボールの予測上の速度を返す。
        /// </summary>
        Vector3 Velocity(std::size_t i) const { return At(i).physics.velocity; }
        /// <summary>
        /// ゲーム終了が予測されるまでシミュレーションを続ける。
        /// </summary>
        bool ShouldContinue() const { return !gameOver; }

        /// <summary>
        /// 撃破済みの敵を除外し、プレイヤーにはショット補正を加えて攻撃力を返す。
        /// </summary>
        int Attack(const Ball& ball) const
        {
            if (ball.physics.enemy && ball.defeated) return 0;
            return ball.attack + (ball.physics.player ? result.shot.collisionBonus : 0);
        }

        /// <summary>
        /// プレイヤーボールの予測位置を、間隔と進行方向を考慮して軌跡に追加する。
        /// </summary>
        void AddPoint(const Ball& ball, bool force = false)
        {
            if (ball.physics.id != playerId || !ball.active) return;
            const auto& p = ball.physics.position;
            const auto& v = ball.physics.velocity;
            const float lengths = lastPathVelocity.Length() * v.Length();
            const bool turned = lengths > 0.0001f && lastPathVelocity.Dot(v) / lengths < 0.9999f;
            if (!result.path.empty() && !force && !turned && (p - result.path.back()).LengthSquared() < 1.0f) return;
            if (!result.path.empty() && p == result.path.back()) return;
            if (result.path.size() >= MaxPathPoints) { result.pathTruncated = true; return; }
            result.path.push_back(p);
            lastPathVelocity = v;
        }

        /// <summary>
        /// サブステップ開始位置を各ボール分保存し、軌跡へ反映する。
        /// </summary>
        void BeginSubstep()
        {
            for (std::size_t i = 0; i < Count(); ++i)
            {
                starts[i] = At(i).physics.position;
                AddPoint(At(i));
            }
        }
		/// <summary>
		/// 予測内でボールを進め、活動中の敵の移動を戦闘予測に反映する。
		/// </summary>
		void Move(std::size_t i, float interval)
		{
			Ball& ball = At(i);
			const Vector3 from = ball.physics.position;
			ball.physics.position += Velocity(i) * interval;
			if (!ball.physics.enemy || ball.defeated) return;
			Vector3 movement = ball.physics.position - from;
			movement.y = 0.0f;
			HeavyCollisionRules::RecordEnemyMovement(
				result.heavyCollisions,
				ball.physics.id,
				movement.Length(),
				AxisStackGeneration::HeavyDistance(result.shotCategory, result.shotStatus));
		}

        /// <summary>
        /// プレイヤーボールの反射位置を記録し、初回接触の案内情報を設定する。
        /// </summary>
        void RecordPreviewReflection(const Ball& ball, const Ball* other = nullptr)
        {
            if (ball.physics.id != playerId) return;
            // Only the first blocking contact supplies the short object-ball guide.
            // Later contacts (including enemies reached after a wall) stay hidden.
            if (result.reflectionPathIndices.empty() && other)
            {
                auto& guide = result.initialContactGuide;
                guide.hitBall = true;
                guide.hitEnemy = other->physics.enemy;
                guide.chainImpactCenter = other->physics.enemy || other->physics.breakBall;
                guide.playerPosition = ball.physics.position;
                guide.ballPosition = other->physics.position;
                guide.ballDirection = other->physics.velocity;
                if (guide.ballDirection.LengthSquared() > 0.0001f) guide.ballDirection.Normalize();
            }
            AddPoint(ball, true);
            if (!result.pathTruncated && !result.path.empty())
                result.reflectionPathIndices.push_back(result.path.size() - 1);
        }

        /// <summary>
        /// ボールが進入したポケットを特定し、落下に伴う予測上の効果を処理する。
        /// </summary>
        bool CheckPocket(Ball& ball, const Vector3& start)
        {
            for (const auto& pocket : pockets)
            {
                if (!BallPhysicsRules::PocketHit(start, ball.physics.position, ball.physics.status.radius, pocket)) continue;
                AddPoint(ball, true);
                const int hpBefore = ball.hp;
				const bool wasDefeated = ball.defeated;
                if (ball.physics.player)
                {
                    const int hpDamage = PlayerShieldRules::Absorb(
                        result.playerShieldRemaining, playerPocketDamage);
                    result.playerShieldAbsorbed += (std::max)(0, playerPocketDamage) - hpDamage;
                    ball.hp = (std::max)(0, ball.hp - hpDamage);
                    ball.defeated = ball.hp == 0;
                    gameOver = ball.defeated;
                    if (!ball.defeated)
                    {
                        ball.pocketed = true;
                        ball.physics.position = Vector3(0, -1000, 0);
                    }
                }
                else if (ball.physics.boss && !wasDefeated)
                {
                    std::vector<BossPocketRules::Obstacle> obstacles;
                    for (const auto& other : result.balls)
                        if (&other != &ball && other.active)
                            obstacles.push_back({other.physics.position, other.physics.status.radius});
                    const auto position = BossPocketRules::FindReturnPosition(ball.physics.position,
                        ball.physics.status.radius, obstacles, pockets);
                    if (!position)
                    {
                        ball.physics.velocity = ball.acceleration = Vector3::Zero;
                        return true;
                    }
                    const auto effect = BossCombatRules::ResolvePocket(ball.bossState, ball.hp, ball.maxHp);
                    ball.hp -= effect.damage;
                    result.damage += effect.damage;
                    result.bossPocketDamage += effect.damage;
                    result.bossPocketCount += effect.applied ? 1 : 0;
                    ball.defeated = ball.hp == 0;
                    if (!ball.defeated)
                    {
                        ball.physics.position = *position;
                        ball.physics.velocity = ball.acceleration = Vector3::Zero;
                        ball.physics.pierceUses = 0;
                        ball.physics.pierced.clear();
                        ball.physics.touching.clear();
                        return true;
                    }
                    HandleDefeatAnchors(ball);
                }
                else if (!ball.enemyId.empty())
                {
					ball.hp = 0;
					ball.defeated = true;
                    result.damage += hpBefore - ball.hp;
					if (!wasDefeated && ball.defeated)
						HandleDefeatAnchors(ball);
                }
                if (ball.physics.breakBall)
                {
                    ball.pocketed = true;
                    ball.breakBallUsed = false;
                    ball.physics.position = Vector3(0, -1000, 0);
                }
                ball.active = false;
                ball.physics.velocity = ball.acceleration = Vector3::Zero;
                return true;
            }
            return false;
        }

        /// <summary>
        /// ポケット侵入と壁への反射を処理し、予測の軌跡と効果を更新する。
        /// </summary>
        void Environment(Ball& ball, const Vector3& start)
        {
            if (!ball.active || CheckPocket(ball, start)) return;
			const auto resolveWall = [&](const Collision::Segment& wall,
				const Vector3& interiorReference)
			{
				const Vector3 velocityBefore = ball.physics.velocity;
				if (!BallPhysicsRules::Wall(
					ball.physics, wall, interiorReference))
				{
					return;
				}
				if (ball.physics.player)
				{
					result.shot.Wall();
					CushionChargeRules::ReflectionConfig config;
					config.generatesStacks = result.shotCategory == BallCategory::Bounce ||
						result.shot.ballId == "player_cushion_charge";
					config.generationLimit = AxisStackGeneration::ReflectionLimit(
						result.shotCategory, result.shotStatus);
					config.stacksPerReflection = AxisStackGeneration::ReflectionAmount(
						result.shotCategory, result.shotStatus);
					config.consumesStacks =
						result.shot.ballId == "player_bounce";
					config.consumptionLimit =
						result.shotStatus.cushionConsumptionLimit;
					config.reflectedSpeedMultiplier =
						result.shotStatus.cushionReflectionSpeedMultiplier;
					const auto cushion = CushionChargeRules::ApplyValidReflection(
						result.cushionCharges, config, ball.physics.velocity);
					result.cushionBoostConsumed |= cushion.consumed > 0;
					result.cushionStrongUses =
						result.cushionCharges.consumedThisShot;
				}
				if (ball.physics.enemy &&
					CushionChargeRules::IsPinballTarget(
						result.cushionCharges, ball.physics.id))
				{
					CushionChargeRules::RegisterPinballCollision(
						result.cushionCharges, ball.physics.id);
					CushionChargeRules::PreservePinballSpeed(
						result.cushionCharges,
						ball.physics.id,
						velocityBefore,
						ball.physics.velocity);
				}
				RecordPreviewReflection(ball);
			};
            if (ball.physics.boss)
                for (const auto& wall : BallPhysicsRules::BossWalls())
					resolveWall(wall, Vector3::Zero);
            for (const auto& wall : walls)
            {
				resolveWall(wall, interior);
            }
            CheckPocket(ball, start);
        }
        /// <summary>
        /// 指定ボールの移動区間に対して壁とポケットの判定を実行する。
        /// </summary>
        void ResolveEnvironment(std::size_t i) { Environment(At(i), starts[i]); }

		/// <summary>
		/// 敵から消費するアンカースタックを計算し、プレイヤーの蓄積値と変換進捗を更新する。
		/// </summary>
		AnchorStackRules::EnemyConsumeResult ConsumeEnemyAnchors(
			Ball& target,
			int amount)
		{
			AnchorStackRules::State state;
			state.playerStacks = result.playerAnchorStacks;
			state.conversionProgress = result.anchorConversionProgress;
			const auto consumed = AnchorStackRules::ConsumeEnemyStacks(
				target.anchorStacks,
				state,
				amount,
				result.shotStatus.anchorStackMax);
			result.playerAnchorStacks = state.playerStacks;
			result.anchorConversionProgress = state.conversionProgress;
			return consumed;
		}

		/// <summary>
		/// 敵の撃破時に必要なアンカースタックを消費し、敵の蓄積をリセットする。
		/// </summary>
		void HandleDefeatAnchors(Ball& target)
		{
			const int automaticConsume =
				AnchorStackRules::DefeatAutoConsumeAmount(target.anchorStacks);
			if (automaticConsume > 0)
				ConsumeEnemyAnchors(target, automaticConsume);
			target.anchorStacks = 0;
		}

		/// <summary>
		/// 敵に方向補正などを含むダメージを適用し、予測のHPと撃破情報を更新する。
		/// </summary>
		void Damage(Ball& target, int amount, const Vector3& source, bool directional = true)
		{
			if (!target.physics.enemy || target.defeated || amount <= 0) return;
			const bool wasDefeated = target.defeated;
			if (directional)
			{
				amount = BallPhysicsRules::DirectionalDamage(amount, target.physics.position, source, target.frontalMultiplier);
				amount = EnemyGimmickRules::ScaleCollisionDamage(
					amount,
					EnemyGimmickRules::CollisionDamageMultiplier(
						target.collisionDamageMultipliers,
						target.collisionStage));
			}
            const int before = target.hp;
            const int applied = target.physics.boss ? BossCombatRules::DirectDamage(amount, target.defense, target.bossState) :
                (std::max)(1, amount - target.defense);
            target.hp = (std::max)(0, target.hp - applied);
            target.defeated = target.hp == 0;
            result.damage += before - target.hp;
			if (!wasDefeated && target.defeated)
            {
                HandleDefeatAnchors(target);
                target.physics.enemy = false;
            }
            // EnemyBall::TakeDamage intentionally preserves velocity after lethal contact.
        }

		/// <summary>
		/// 相手の衝突猶予を確認し、敵の衝突段階を進めて次の接触を登録する。
		/// </summary>
		void AdvanceCollisionStage(Ball& target, std::uintptr_t otherId)
		{
			if (!target.physics.enemy || target.defeated ||
				target.collisionDamageMultipliers.empty() || otherId == 0 ||
				target.collisionGraceByBall.contains(otherId)) return;
			++target.collisionStage;
			if (target.collisionGraceTicks > 0)
				target.collisionGraceByBall[otherId] = target.collisionGraceTicks;
		}

		/// <summary>
		/// 連鎖が成立した接触に対し、ヘビー系のフィニッシャーや追加効果を適用する。
		/// </summary>
		void ChainImpact(const Ball& playerBall, const Vector3& center,
			std::uintptr_t excludedTargetId, int attackDamage, bool suppressed)
		{
			if (suppressed) return;
			if (result.shot.ballId == "player_chain_impact" &&
				!result.heavyFinisherConsumed)
			{
				result.heavyFinisherConsumed = true;
				const auto finisher = HeavyCollisionRules::ConsumeForFinisher(
					result.heavyCollisions,
					result.shotStatus.heavyFinisherDamagePerCollision,
					result.shotStatus.heavyCollisionConsumeAmount);
				result.heavyCollisionCount = result.heavyCollisions.stackCount;
				attackDamage += finisher.bonusDamage;
			}
			const float radius = playerBall.physics.status.chainImpactRadius;
			if (radius <= 0.0f || attackDamage <= 0) return;
			struct Target
			{
				Ball* ball = nullptr;
				float distanceSquared = 0.0f;
				std::size_t stableOrder = 0;
			};
			std::vector<Target> targets;
			for (std::size_t index = 0; index < result.balls.size(); ++index)
			{
				Ball& target = result.balls[index];
				if (!target.physics.enemy || target.physics.id == excludedTargetId ||
					target.defeated || target.pocketed || !target.active) continue;
				Vector3 offset = target.physics.position - center;
				offset.y = 0.0f;
				const float distanceSquared = offset.LengthSquared();
				if (distanceSquared <= radius * radius)
					targets.push_back({ &target, distanceSquared, index });
			}
			std::sort(targets.begin(), targets.end(),
				[](const Target& left, const Target& right)
				{
					if (left.distanceSquared != right.distanceSquared)
						return left.distanceSquared < right.distanceSquared;
					return left.stableOrder < right.stableOrder;
				});
			if (targets.size() > 2) targets.resize(2);
			for (const Target& target : targets)
			{
				Damage(*target.ball, attackDamage, center, false);
				++result.chainImpactHits;
			}
		}

        /// <summary>
        /// 予測内で2球の衝突を解決し、反射・貫通・ダメージと能力の連鎖を更新する。
        /// </summary>
        void ResolvePair(std::size_t i, std::size_t j)
        {
            Ball& a = At(i);
            Ball& b = At(j);
            const Vector3 ap = a.physics.position, bp = b.physics.position;
            const Vector3 av = a.physics.velocity, bv = b.physics.velocity;
			const int aPierceUses = a.physics.pierceUses, bPierceUses = b.physics.pierceUses;
			const bool aWasDefeated = a.defeated, bWasDefeated = b.defeated;
            if (BallPhysicsRules::Pair(a.physics, b.physics) && !gameOver)
            {
                // Count a physical ball response once, never the solver's repeated
                // overlap corrections or a straight-through piercing contact.
                if (a.physics.pierceUses == aPierceUses && b.physics.pierceUses == bPierceUses)
                {
                    RecordPreviewReflection(a, &b);
                    RecordPreviewReflection(b, &a);
                }
                if ((a.bossMinion && b.physics.boss && !b.defeated) || (b.bossMinion && a.physics.boss && !a.defeated))
                {
                    Ball& minion = a.bossMinion ? a : b;
                    Ball& boss = a.physics.boss ? a : b;
                    if (!minion.defeated) { minion.defeated=true; HandleDefeatAnchors(minion); }
                    const auto effect = BossEncounterRules::MinionImpact(boss.bossState,boss.hp);
                    boss.hp -= effect.damage;
                    result.damage += effect.damage; result.bossFixedDamage += effect.damage; ++result.breakBallHits;
                    boss.defeated = boss.hp==0;
                    if (boss.defeated) { HandleDefeatAnchors(boss); boss.physics.enemy=false; }
                    minion.hp=0; minion.physics.enemy=false; minion.active=false;
                    minion.physics.position=Vector3(0,-1000,0);
                    minion.physics.velocity=minion.acceleration=Vector3::Zero;
                    minion.physics.pierceUses=0; minion.physics.pierced.clear(); minion.physics.touching.clear();
                    return;
                }
                if (a.physics.breakBall || b.physics.breakBall)
                {
                    const Vector3 aPosition = a.physics.position;
                    const Vector3 bPosition = b.physics.position;
                    auto hit = [&](Ball& neutral, Ball& boss) {
                        if (!neutral.physics.breakBall || !boss.physics.boss || boss.defeated) return;
                        boss.bossState.HitBreakBall();
                        const int before = boss.hp;
                        boss.hp = (std::max)(0, before - BossCombatRules::BreakBallDamage);
                        boss.defeated = boss.hp == 0;
                        if (boss.defeated)
                        {
                            HandleDefeatAnchors(boss);
                            // HitBreakBall retains the corpse as a physical
                            // sphere, but runtime no longer treats it as a
                            // pierce/forced-exit enemy target.
                            boss.physics.enemy = false;
                        }
                        result.damage += before - boss.hp;
                        result.bossFixedDamage += before - boss.hp;
                        ++result.breakBallHits;
                        neutral.active = false;
                        neutral.breakBallUsed = true;
                        neutral.pocketed = false;
                        neutral.physics.position = Vector3(0, -1000, 0);
                        neutral.physics.velocity = neutral.acceleration = Vector3::Zero;
                    };
                    hit(a, b); hit(b, a);
                    if (a.physics.breakBall && b.physics.player)
                        ChainImpact(b, aPosition, 0, Attack(b), false);
                    if (b.physics.breakBall && a.physics.player)
                        ChainImpact(a, bPosition, 0, Attack(a), false);
                    if (BallPhysicsRules::StopAnchor(a.physics, a.acceleration)) result.shot.Anchor();
                    if (BallPhysicsRules::StopAnchor(b.physics, b.acceleration)) result.shot.Anchor();
                }
                else
                {
				const bool playerEnemy = (a.physics.player && b.physics.enemy) || (b.physics.player && a.physics.enemy);
				const bool enemyEnemy = a.physics.enemy && b.physics.enemy;
				Ball* targetEnemy = a.physics.enemy ? &a : (b.physics.enemy ? &b : nullptr);
				Ball* playerBall = a.physics.player ? &a : (b.physics.player ? &b : nullptr);
				auto recordPierce = [&](const Ball& playerBall, const Ball& enemyBall)
				{
					if (enemyBall.defeated || enemyBall.pocketed) return;
					result.uniquePiercedEnemies.insert(enemyBall.physics.id);
					PierceTraceRules::RecordPierce(
						result.pierceTraces,
						enemyBall.physics.id,
						1 + result.shotStatus.eventStackGenerationBonus);
					Vector3 direction = playerBall.physics.velocity;
					direction.y = 0.0f;
					if (direction.LengthSquared() <= 0.0001f) return;
					direction.Normalize();
					PierceTraceRules::AddTrace(
						result.pierceTraces,
						playerBall.physics.position,
						playerBall.physics.position +
							direction * result.shotStatus.traceUseDistance);
				};
				if (a.physics.player && a.physics.pierceUses > aPierceUses && b.physics.enemy)
					recordPierce(a, b);
				if (b.physics.player && b.physics.pierceUses > bPierceUses && a.physics.enemy)
					recordPierce(b, a);
				const bool playerHitCombatEnemy =
					playerEnemy && targetEnemy != nullptr &&
					!targetEnemy->defeated && !targetEnemy->pocketed;
				const bool combatEnemyPair = enemyEnemy &&
					!a.defeated && !a.pocketed &&
					!b.defeated && !b.pocketed;
				if (playerHitCombatEnemy &&
					result.shotStatus.abilities.lightweight)
				{
					HeavyCollisionRules::GenerateOncePerEnemy(
						result.heavyCollisions,
						targetEnemy->physics.id,
						1 + result.shotStatus.eventStackGenerationBonus);
				}
				if (combatEnemyPair)
				{
					const int beforeShield = result.playerShieldRemaining;
                    result.playerShieldRemaining = std::clamp(beforeShield +
                        (std::max)(0, result.shotStatus.enemyCollisionShield), 0, 100000);
                    result.stopShieldGranted += result.playerShieldRemaining - beforeShield;
					AnchorStackRules::ShareEnemyStacks(
						a.anchorStacks,
						b.anchorStacks,
						result.shotStatus.anchorStackMax);
				}
				auto damage = BallPhysicsRules::ContactDamage(a.physics, b.physics, Attack(a), Attack(b),
                    a.defeated, b.defeated, result.shot, result.shotStatus.impactAttack);
				int pinballDamageStage = 0;
				if (combatEnemyPair)
				{
					Ball* pinballTarget = nullptr;
					const Vector3* velocityBefore = nullptr;
					if (CushionChargeRules::IsPinballTarget(
						result.cushionCharges, a.physics.id))
					{
						pinballTarget = &a;
						velocityBefore = &av;
					}
					else if (CushionChargeRules::IsPinballTarget(
						result.cushionCharges, b.physics.id))
					{
						pinballTarget = &b;
						velocityBefore = &bv;
					}
					if (pinballTarget != nullptr && velocityBefore != nullptr)
					{
						pinballDamageStage =
							CushionChargeRules::RegisterPinballCollision(
								result.cushionCharges,
								pinballTarget->physics.id);
						CushionChargeRules::PreservePinballSpeed(
							result.cushionCharges,
							pinballTarget->physics.id,
							*velocityBefore,
							pinballTarget->physics.velocity);
					}
					damage.first = HeavyCollisionRules::ScaleEnemyCollisionDamage(
						result.heavyCollisions, damage.first);
					damage.second = HeavyCollisionRules::ScaleEnemyCollisionDamage(
						result.heavyCollisions, damage.second);
					damage.first += pinballDamageStage;
					damage.second += pinballDamageStage;
				}
				if (playerHitCombatEnemy)
				{
					int bonus = result.synergyDamageBonus;
					if (result.shot.ballId == "player_bounce" && targetEnemy != nullptr &&
						CushionChargeRules::ConsumeEmpoweredHit(
							result.cushionCharges, targetEnemy->physics.id))
					{
						bonus += result.shotStatus.cushionBounceAttackBonus;
					}
					if (result.shot.ballId == "player_pierce_finisher" &&
						!result.uniquePiercedEnemies.empty())
						bonus += result.shotStatus.pierceFinisherBaseBonus +
							(std::max)(0, static_cast<int>(result.uniquePiercedEnemies.size()) - 1) *
							result.shotStatus.pierceFinisherMultiTargetBonus;
					if (result.shot.ballId == "player_ricochet_finisher" &&
						targetEnemy != nullptr)
					{
						const bool activated = CushionChargeRules::ActivatePinball(
							result.cushionCharges,
							targetEnemy->physics.id,
							result.shotStatus.ricochetStackConsume,
							result.shotStatus.pinballCollisionsPerStage,
							result.shotStatus.pinballSpeedRetention);
						if (CushionChargeRules::IsPinballTarget(
							result.cushionCharges, targetEnemy->physics.id))
						{
							CushionChargeRules::RegisterPinballCollision(
								result.cushionCharges,
								targetEnemy->physics.id);
							CushionChargeRules::PreservePinballSpeed(
								result.cushionCharges,
								targetEnemy->physics.id,
								a.physics.enemy ? av : bv,
								targetEnemy->physics.velocity);
						}
						if (activated && playerBall != nullptr)
						{
							playerBall->physics.velocity = Vector3::Zero;
							playerBall->acceleration = Vector3::Zero;
						}
					}
					if (result.shot.ballId == "player_stop_shield" &&
						targetEnemy != nullptr && targetEnemy->anchorStacks > 0)
					{
						const auto consumed = ConsumeEnemyAnchors(
							*targetEnemy,
							result.shotStatus.anchorEnemyConsumeLimit);
						const int shieldGain = AnchorStackRules::CalculateShield(
							consumed.consumed,
							result.shotStatus.stopShieldPerStack);
						const int beforeShield = result.playerShieldRemaining;
                        result.playerShieldRemaining = (std::min)(
                            result.shotStatus.stopShieldCapacity, beforeShield + shieldGain);
                        result.stopShieldGranted += result.playerShieldRemaining - beforeShield;
					}
					if (result.shot.ballId == "player_anchor_finisher" &&
						targetEnemy != nullptr && targetEnemy->anchorStacks > 0 &&
						!result.anchorFinisherTriggered)
					{
						result.anchorFinisherTriggered = true;
						const int damagePerStack =
							AnchorStackRules::FinisherDamagePerEnemyStack(
								result.playerAnchorStacks);
						const int available = targetEnemy->anchorStacks;
						const int requested =
							result.shotStatus.anchorFinisherStackConsume <= 0
							? available
							: (std::min)(available,
								result.shotStatus.anchorFinisherStackConsume);
						const int directBonus = requested * damagePerStack;
						bonus += directBonus;
						const auto consumed = ConsumeEnemyAnchors(
							*targetEnemy, requested);
						const int aoeDamage = directBonus / 2;
						if (consumed.consumed >=
								result.shotStatus.anchorFinisherAoeThreshold &&
							aoeDamage > 0 &&
							result.shotStatus.anchorFinisherAoeRadius > 0.0f)
						{
							const float radiusSquared =
								result.shotStatus.anchorFinisherAoeRadius *
								result.shotStatus.anchorFinisherAoeRadius;
							for (Ball& other : result.balls)
							{
								if (!other.physics.enemy ||
									other.physics.id == targetEnemy->physics.id ||
									other.defeated || other.pocketed || !other.active)
								{
									continue;
								}
								Vector3 offset = other.physics.position -
									targetEnemy->physics.position;
								offset.y = 0.0f;
								if (offset.LengthSquared() <= radiusSquared)
									Damage(other, aoeDamage,
										targetEnemy->physics.position, false);
							}
						}
					}
					if (a.physics.enemy) damage.first += bonus;
					else damage.second += bonus;
				}
				const int anchorAmount = AxisStackGeneration::AnchorAmount(
					result.shotCategory, result.shotStatus);
				if (playerHitCombatEnemy &&
					!result.anchorGrantCommitted &&
					anchorAmount > 0)
				{
					AnchorStackRules::GrantEnemyStacksOnCollision(
						targetEnemy->anchorStacks,
						anchorAmount,
						result.playerAnchorStacks,
						result.shotStatus.anchorPlayerStacksPerBonus,
						result.shotCategory == BallCategory::Anchor ? result.anchorNextGrantBonus : 0,
						result.shotStatus.anchorStackMax);
					result.anchorGrantCommitted = true;
				}
				Damage(a, damage.first, b.physics.position);
				Damage(b, damage.second, a.physics.position);
				if (playerEnemy || enemyEnemy)
				{
					AdvanceCollisionStage(a, b.physics.id);
					AdvanceCollisionStage(b, a.physics.id);
				}
				if (a.physics.player && b.physics.enemy)
					ChainImpact(a, b.physics.position, b.physics.id, damage.second, bWasDefeated);
				else if (b.physics.player && a.physics.enemy)
					ChainImpact(b, a.physics.position, a.physics.id, damage.first, aWasDefeated);
                if (playerEnemy)
                {
                    const bool stoppedA = BallPhysicsRules::StopAnchor(
                        a.physics, a.acceleration);
                    const bool stoppedB = BallPhysicsRules::StopAnchor(
                        b.physics, b.acceleration);
                    if (playerHitCombatEnemy && (stoppedA || stoppedB))
                        result.shot.Anchor();
                }
                if (playerHitCombatEnemy || combatEnemyPair)
                    result.shot.Contact(playerHitCombatEnemy);
                }
                AddPoint(a, true);
                AddPoint(b, true);
            }
            if (a.active && (a.physics.position != ap || a.physics.velocity != av)) Environment(a, ap);
            if (b.active && (b.physics.position != bp || b.physics.velocity != bv)) Environment(b, bp);
        }

        /// <summary>
        /// 活動中のボールの処理順を作り直し、衝突の猶予時間を進める。
        /// </summary>
        void BeginTick()
        {
            order.clear();
            for (std::size_t i = 0; i < result.balls.size(); ++i)
                if (result.balls[i].active) order.push_back(i);
            for (auto i : order)
            {
                for (auto it = result.balls[i].collisionGraceByBall.begin();
                    it != result.balls[i].collisionGraceByBall.end();)
                {
                    if (--it->second <= 0)
                        it = result.balls[i].collisionGraceByBall.erase(it);
                    else ++it;
                }
                auto& body = result.balls[i].physics;
                body.velocity.y = 0.0f;
                const auto missing = [&](std::uintptr_t id) {
                    return std::none_of(order.begin(), order.end(), [&](std::size_t k) { return result.balls[k].physics.id == id; });
                };
                std::erase_if(body.pierced, missing);
                std::erase_if(body.touching, missing);
            }
            std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
                const auto& x = result.balls[a];
                const auto& y = result.balls[b];
                return BallPhysicsRules::OrderingKey(x.physics, x.hp, Attack(x), x.enemyId) <
                    BallPhysicsRules::OrderingKey(y.physics, y.hp, Attack(y), y.enemyId);
            });
            starts.resize(order.size());
        }
    };

    /// <summary>
    /// ボス本体とミニオンの状態から、予測内でボス戦が完了したかを判定する。
    /// </summary>
    std::pair<bool,int> BossCompletion(const std::vector<Ball>& balls)
    {
        bool encounter = false, livingCore = false;
        for (const auto& ball : balls)
        {
            encounter = encounter || ball.physics.boss || ball.bossMinion;
            livingCore = livingCore || (ball.physics.boss && ball.active && !ball.defeated && !ball.pocketed);
        }
        const bool cleared = BossEncounterRules::EncounterCleared(encounter,livingCore);
        int retiring = 0;
        for (const auto& ball : balls)
            if (ball.bossMinion && ball.active && (ball.defeated || cleared)) ++retiring;
        return {cleared,retiring};
    }

    /// <summary>
    /// 生存中のボスについて、アーマー回復の予告が発生するかを求める。
    /// </summary>
    bool BossRecoveryAnnouncement(const std::vector<Ball>& balls)
    {
        for (const auto& ball : balls)
        {
            if (!ball.physics.boss || !ball.active || ball.defeated || ball.pocketed) continue;
            auto turn = ball.bossTurn;
            auto armor = ball.bossState;
            return BossEncounterRules::CompleteShot(turn,armor) &&
                turn.intent == BossEncounterRules::Intent::Strong;
        }
        return false;
    }

    /// <summary>
    /// 生存中のボスとミニオンの数を確認し、敵ターンで発生する補給を求める。
    /// </summary>
    std::pair<bool,int> BossAdvanceSupply(const std::vector<Ball>& balls)
    {
        int living = 0;
        for (const auto& ball : balls)
            if (ball.bossMinion && ball.active && !ball.defeated && !ball.pocketed) ++living;
        for (const auto& ball : balls)
        {
            if (!ball.physics.boss || !ball.active || ball.defeated || ball.pocketed) continue;
            auto turn = ball.bossTurn;
            auto armor = ball.bossState;
            const bool used = turn.advanceSummonConsumed;
            BossEncounterRules::CompleteShot(turn,armor);
            const int count = BossEncounterRules::EndEnemyTurn(turn,armor,living);
            const bool advance = !used && turn.advanceSummonConsumed;
            return {advance,advance ? count : 0};
        }
        return {false,0};
    }

    /// <summary>
    /// 予測ボールの識別情報・位置・速度・戦闘状態を検証用JSONに変換する。
    /// </summary>
    nlohmann::json BallJson(const Ball& ball)
    {
        auto vector = [](const Vector3& v) { return nlohmann::json::array({v.x, v.y, v.z}); };
        return {{"id", ball.physics.id}, {"player", ball.physics.player}, {"enemy_id", ball.enemyId},
            {"position", vector(ball.physics.position)}, {"velocity", vector(ball.physics.velocity)},
            {"hp", ball.hp}, {"active", ball.active}, {"defeated", ball.defeated}, {"pocketed", ball.pocketed},
            {"boss", ball.physics.boss}, {"boss_minion",ball.bossMinion}, {"break_ball", ball.physics.breakBall}, {"break_ball_used", ball.breakBallUsed},
            {"armor", ball.bossState.armor}, {"break_shots_remaining", ball.bossState.shotsRemaining},
            {"break_started_this_shot", ball.bossState.startedThisShot},
            {"pocket_resolved_this_shot", ball.bossState.pocketResolvedThisShot}};
    }

	/// <summary>
	/// クッションのスタックと補正の状態を検証用JSONに変換する。
	/// </summary>
	nlohmann::json CushionJson(const CushionChargeRules::State& charges)
	{
		return {
			{ "stack_count", charges.stackCount },
			{ "pending_setup_bonus", charges.pendingSetupBonus },
			{ "active_setup_bonus", charges.activeSetupBonus },
			{ "generated_this_shot", charges.generatedThisShot },
			{ "consumed_this_shot", charges.consumedThisShot },
			{ "empowered_hits_remaining", charges.empoweredHitsRemaining },
			{ "pinball_target_id", charges.pinballTargetId },
			{ "pinball_collision_count", charges.pinballCollisionCount },
			{ "pinball_collisions_per_stage", charges.pinballCollisionsPerStage },
		};
	}

    /// <summary>
    /// デバッグ環境でショット予測の検証出力が有効かを判定する。
    /// </summary>
    bool VerificationEnabled()
    {
#ifdef _DEBUG
        char* flag = nullptr;
        std::size_t size = 0;
        _dupenv_s(&flag, &size, "DX22_TEST_PREDICTION");
        const bool enabled = flag != nullptr && std::string(flag) == "1";
        free(flag);
        return enabled;
#else
        return false;
#endif
    }
}

/// <summary>
/// 入力状態を複製してショットをシミュレーションし、軌跡と戦闘結果の予測を返す。
/// </summary>
Result BallShotPrediction::Predict(const Input& input, const PlayerBall& player, const Vector3& velocity, bool skipFirstPlayerFriction, const PlayerBallData* offer)
{
    const auto started = std::chrono::steady_clock::now();
    Result result;
    if (!std::isfinite(velocity.x) || !std::isfinite(velocity.z) || velocity.LengthSquared() <= 0.0001f) return result;
    result.balls = Capture(input);
	result.cushionCharges = input.cushionCharges;
	result.pierceTraces = input.pierceTraces;
	result.heavyCollisions = input.heavyCollisions;
	result.heavyCollisionCount = input.heavyCollisions.stackCount;
	result.playerAnchorStacks = input.playerAnchorStacks;
	result.anchorConversionProgress = input.anchorConversionProgress;
    result.anchorNextGrantBonus = input.anchorNextGrantBonus;
	CushionChargeRules::BeginPlayerShot(result.cushionCharges);
	HeavyCollisionRules::BeginShot(result.heavyCollisions);
	PierceTraceRules::BeginShot(result.pierceTraces);
    result.shot = input.shot;
    if (offer) result.shot.ballId = offer->definitionId;
    result.shotCategory = offer ? offer->category : input.hasCurrentBall
        ? static_cast<BallCategory>(input.currentBallCategory)
        : BallCategoryFromId({}, result.shot.ballId);
    World world{ result };
    world.playerId = reinterpret_cast<std::uintptr_t>(player.GetBall());
    world.playerPocketDamage = input.playerPocketDamage;
    bool found = false;
    for (auto& ball : result.balls)
    {
        if (ball.physics.boss) ball.bossState.BeginShot();
        if (ball.physics.id != world.playerId) continue;
        if (!ball.active) return result;
        ball.physics.velocity = velocity;
		result.shotStatus = ball.physics.status;
        ball.physics.pierceLimit = input.pierceMaximumUses;
        ball.physics.pierceRetention = input.pierceSpeedRetention;
        if (offer)
        {
            ball.physics.status = offer->status;
			result.shotStatus = offer->status;
			ball.defense = 0;
            ball.physics.pierceLimit = offer->status.pierceMaxUses;
            ball.physics.pierceRetention = offer->status.pierceSpeedRetention;
            if (offer->definitionId == "player_pierce" && input.hasPierceBallCharger)
            { ++ball.physics.pierceLimit; ball.physics.pierceRetention = 1.0f; }
        }
        ball.attack = ball.physics.status.attack + input.relicAttackBonus +
            player.GetAuraStatusEffects().GetAttackModifier();
		result.stopShieldGranted = std::clamp(result.shotStatus.shotShield, 0, 100000);
        result.playerShieldRemaining = result.stopShieldGranted;
        world.AddPoint(ball, true);
        found = true;
    }
    if (!found) return result;
	if (result.shot.ballId == "player_collision_boost" &&
		result.shotStatus.heavySetupStackConsume > 0 &&
		result.heavyCollisions.stackCount >=
			result.shotStatus.heavySetupStackConsume)
	{
		HeavyCollisionRules::Consume(
			result.heavyCollisions,
			result.shotStatus.heavySetupStackConsume);
		HeavyCollisionRules::ArmNextEnemyCollisionMultiplier(
			result.heavyCollisions,
			result.shotStatus.heavyEnemyCollisionMultiplier);
	}
	if (result.shot.ballId == "player_minion_finisher")
	{
		const bool finisher = true;
		const int consumeLimit = PierceTraceRules::MaxStackCount;
		const int effectiveConsume =
			PierceTraceRules::CalculateEffectiveMinionStackConsume(
				result.pierceTraces,
				consumeLimit,
				finisher);
		const int consumed = effectiveConsume > 0
			? PierceTraceRules::ConsumeStacks(
				result.pierceTraces,
				effectiveConsume)
			: 0;
		const auto launches = PierceTraceRules::BuildMinionLaunches(
			result.pierceTraces,
			consumed,
			finisher);
		for (std::size_t index = 0; index < launches.size(); ++index)
		{
			const auto& launch = launches[index];
			Ball minion;
			minion.physics.id = (std::numeric_limits<std::uintptr_t>::max)() - index;
			minion.physics.position = launch.start;
			minion.physics.velocity = launch.direction *
				PierceTraceRules::MinionLaunchSpeed;
			minion.physics.status = result.shotStatus;
			minion.physics.status.abilities = {};
			minion.physics.status.attack = launch.attack;
			minion.physics.status.knockbackTransfer = launch.knockbackMultiplier;
			minion.physics.minion = true;
			minion.hp = minion.maxHp = 1;
			minion.attack = launch.attack;
			minion.playerSimulation = false;
			result.balls.push_back(std::move(minion));
		}
	}
    for (auto* frame : input.tableFrames)
    {
        if (!frame->IsEnabled() || !frame->GetGameObject()->IsActive()) continue;
        const auto walls = frame->GetWalls();
        world.walls.insert(world.walls.end(), walls.begin(), walls.end());
    }
    for (const auto& wall : world.walls) world.interior += wall.start + wall.end;
    if (!world.walls.empty()) world.interior /= static_cast<float>(world.walls.size() * 2);
    for (auto* pocket : input.pockets)
        if (pocket && pocket->GetGameObject() && pocket->GetGameObject()->IsActive()) world.pockets.push_back(pocket->GetSphere());
    int stoppedTicks = 0;
    for (int tick = 0; tick < MaxTicks; ++tick)
    {
        for (auto& ball : result.balls)
        {
            if (!ball.active) continue;
            if (ball.physics.player && !(tick == 0 && skipFirstPlayerFriction))
            {
                Vector3 exitDirection;
                bool exitingPierce = false;
                if (ball.physics.status.abilities.pierce && !ball.physics.pierced.empty())
                    for (const auto& other : result.balls)
                        if (other.active && BallPhysicsRules::PierceExitDirection(ball.physics, other.physics, exitDirection))
                        { exitingPierce = true; break; }
                if (!ball.playerSimulation &&
                    (exitingPierce || BallPhysicsRules::HasResidualMotion(ball.physics.velocity)))
                {
                    ball.playerSimulation = true;
                    ball.stopCount = 0;
                }
                if (ball.playerSimulation)
                    ball.playerSimulation = !BallPhysicsRules::PlayerFriction(ball.physics.velocity, ball.acceleration,
                    ball.physics.status, ball.physics.status.friction, ball.stopCount,
                    exitingPierce ? &exitDirection : nullptr);
            }
            if (!ball.enemyId.empty() || ball.physics.minion ||
				ball.physics.breakBall || ball.nuisanceBall)
                BallPhysicsRules::EnemyFriction(ball.physics.velocity, ball.physics.status.friction);
        }
        world.BeginTick();
        const auto step = ContinuousBallStepper::Step(world);
        ++result.ticks;
        result.substeps += step.substeps;
        const bool stopped = std::all_of(result.balls.begin(), result.balls.end(), [](const Ball& b) {
            return b.physics.velocity.LengthSquared() < 0.03f;
        });
        stoppedTicks = stopped ? stoppedTicks + 1 : 0;
        if (step.limitReached) break;
        if (world.gameOver || stoppedTicks >= 11)
        {
            // BattleController::CompleteShot normalizes residual sub-threshold
            // motion before exposing the next aiming state.
            if (!world.gameOver)
                for (auto& ball : result.balls)
                    ball.physics.velocity = ball.acceleration = Vector3::Zero;
            result.complete = true;
            break;
        }
        if (result.substeps >= MaxTotalSubsteps) break;
    }
    for (const auto& ball : result.balls) world.AddPoint(ball, true);
	HeavyCollisionRules::EndShot(
		result.heavyCollisions,
		result.shotStatus.abilities.heavyDistanceStacks
			? result.shotStatus.eventStackGenerationBonus : 0);
	result.heavyCollisionCount = result.heavyCollisions.stackCount;
    if (result.complete)
    {
        const auto completion = BossCompletion(result.balls);
        result.bossEncounterCleared = completion.first;
        result.bossMinionsRetiredAfterShot = completion.second;
        result.bossRecoveryStrongAttackAnnounced = BossRecoveryAnnouncement(result.balls);
        const auto supply = BossAdvanceSupply(result.balls);
        result.bossAdvanceSupplyAttemptedIfTurnContinues = supply.first;
        result.bossAdvanceSupplyRequestedIfTurnContinues = supply.second;
    }
    result.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    return result;
}

/// <summary>
/// 予測に影響するボールや戦闘の状態をまとめ、入力を識別するハッシュを計算する。
/// </summary>
std::uint64_t BallShotPrediction::WorldKey(const Input& input)
{
    std::uint64_t hash = 14695981039346656037ull;
    auto add = [&](std::uint64_t value) { hash ^= value; hash *= 1099511628211ull; };
    auto scalar = [&](float value) { add(std::bit_cast<std::uint32_t>(value)); };
    auto vector = [&](const Vector3& v) { scalar(v.x); scalar(v.y); scalar(v.z); };
    for (const auto& ball : Capture(input))
    {
        add(ball.physics.id); add(ball.active); add(ball.hp); add(ball.maxHp); add(ball.attack); add(ball.defense);
        add(ball.defeated); add(ball.pocketed);
        add(ball.physics.boss); add(ball.physics.breakBall); add(ball.breakBallUsed);
		add(ball.anchorStacks);
        add(ball.bossState.armor); add(ball.bossState.shotsRemaining); add(ball.bossState.startedThisShot);
        add(ball.bossState.pocketResolvedThisShot);
        add(ball.bossMinion); add(ball.plannedAttackDamage);
        add(static_cast<std::uint64_t>(ball.bossTurn.intent)); add(ball.bossTurn.turn); add(ball.bossTurn.interruptedThisShot);
        add(ball.bossTurn.advanceSummonConsumed); add(ball.bossTurn.summonCycle); add(ball.bossTurn.nextMinionSerial);
        vector(ball.physics.position); vector(ball.physics.velocity);
        const auto& s = ball.physics.status;
        scalar(s.radius); scalar(s.mass); scalar(s.restitution); scalar(s.friction); scalar(s.knockbackTransfer);
		scalar(s.lightweightKnockbackMultiplier);
		scalar(s.lightweightSpeedLossReduction);
		add(s.eventStackGenerationBonus);
		add(s.heavySetupStackConsume);
		scalar(s.heavyEnemyCollisionMultiplier);
		add(s.heavyCollisionConsumeAmount);
		scalar(s.heavyFinisherDamagePerCollision);
        add(s.abilities.pierce); add(s.abilities.anchor); add(s.pierceMaxUses); scalar(s.pierceSpeedRetention);
        scalar(s.anchorBrakeMultiplier); scalar(s.anchorStopSpeedSquared); add(s.anchorKnockbackImmune);
        scalar(s.cushionChargeSpeedMultiplier);
		add(s.stopShieldPerStack); add(s.stopShieldCapacity);
        add(s.impactAttack); add(s.shotShield); add(s.enemyCollisionShield);
		scalar(s.chainImpactRadius); add(s.abilities.refractAfterPierce);
		add(s.traceDurability); scalar(s.traceUseAngleTolerance); scalar(s.traceUseDistance); scalar(s.traceWidth);
		add(s.tracePierceMaxUsesBonus); scalar(s.tracePierceSpeedRetentionBonus); scalar(s.traceNonPierceSpeedMultiplier);
		add(s.cushionStackGenerateAmount); add(s.cushionMaxStack); add(s.cushionStackConsumeAmount);
		add(s.cushionBounceAttackBonus); scalar(s.cushionNonBounceSpeedMultiplier);
		add(s.cushionReflectionLimit); add(s.cushionConsumptionLimit);
		scalar(s.cushionReflectionSpeedMultiplier); add(s.reflectionSetupAmount);
		add(s.ricochetStackConsume); add(s.pinballCollisionsPerStage);
		scalar(s.pinballSpeedRetention);
		add(s.anchorPlayerStackGenerate); add(s.anchorEnemyStackGenerate); scalar(s.anchorStackRadius);
		add(s.anchorStackMax); add(s.anchorFinisherStackConsume);
		add(s.anchorFinisherDamagePerStack); add(s.anchorFinisherAoeThreshold); scalar(s.anchorFinisherAoeRadius);
		add(s.anchorEnemyConsumeLimit);
        scalar(ball.frontalMultiplier); scalar(ball.pocketDamageRatio);
        add(ball.collisionStage); add(ball.collisionGraceTicks); add(ball.nuisanceBall);
        for (float multiplier : ball.collisionDamageMultipliers) scalar(multiplier);
    }
    for (bool owned : input.shot.relics) add(owned);
    for (unsigned char c : input.shot.ballId) add(c);
	if (input.hasCurrentBall)
		add(static_cast<std::uint64_t>(input.currentBallCategory));
    add(input.playerPocketDamage);
	add(input.cushionCharges.stackCount);
	add(input.cushionCharges.pendingSetupBonus);
	add(input.cushionCharges.activeSetupBonus);
	add(input.heavyCollisions.stackCount);
	scalar(input.heavyCollisions.distancePerStack);
	add(input.heavyCollisions.gainLimitPerTurn);
	add(input.heavyCollisions.gainedThisTurn);
	scalar(input.heavyCollisions.pendingEnemyCollisionMultiplier);
	scalar(input.heavyCollisions.activeEnemyCollisionMultiplier);
	add(input.playerAnchorStacks);
	add(input.anchorConversionProgress);
    add(input.anchorNextGrantBonus);
	add(input.pierceTraces.stackCount);
	for (const auto& trace : input.pierceTraces.traces)
	{
		add(trace.id); vector(trace.start); vector(trace.end); vector(trace.direction);
	}
    for (auto* frame : input.tableFrames)
    {
        add(frame->IsEnabled()); add(frame->GetGameObject()->IsActive());
        for (const auto& wall : frame->GetWalls()) { vector(wall.start); vector(wall.end); }
    }
    for (auto* pocket : input.pockets)
    {
        add(pocket->GetGameObject()->IsActive());
        const auto sphere = pocket->GetSphere(); vector(sphere.center); scalar(sphere.radius);
    }
    return hash;
}

/// <summary>
/// 検証が有効なとき、同じショットを予測して入力識別子と予測結果を記録する。
/// </summary>
void BallShotPrediction::WriteVerificationPrediction(const Input& input, const PlayerBall& player, const Vector3& velocity, bool skip)
{
    if (!VerificationEnabled()) return;
    const auto before = WorldKey(input);
    const auto result = Predict(input, player, velocity, skip);
    nlohmann::json balls = nlohmann::json::array();
    for (const auto& ball : result.balls) balls.push_back(BallJson(ball));
    const nlohmann::json data = {{"physics_model", BallPhysicsWorld::ModelName}, {"complete", result.complete},
        {"path_truncated", result.pathTruncated}, {"path_points", result.path.size()},
        {"boss_encounter_cleared",result.bossEncounterCleared},
        {"boss_recovery_strong_attack_announced",result.bossRecoveryStrongAttackAnnounced},
        {"boss_advance_supply_attempted_if_turn_continues",result.bossAdvanceSupplyAttemptedIfTurnContinues},
        {"boss_advance_supply_requested_if_turn_continues",result.bossAdvanceSupplyRequestedIfTurnContinues},
        {"boss_minions_retired_after_shot",result.bossMinionsRetiredAfterShot},
        {"reflection_path_indices", result.reflectionPathIndices},
        {"preview_one_reflection_points", result.PreviewPointCount(1)},
        {"preview_one_reflection_limited", result.PreviewReachesLimit(1)},
        {"preview_direct_points", result.PreviewPointCount(0)},
        {"preview_direct_hit_ball", result.initialContactGuide.hitBall},
        {"preview_direct_hit_enemy", result.initialContactGuide.hitEnemy},
        {"preview_chain_impact_center", result.initialContactGuide.chainImpactCenter},
        {"ticks", result.ticks}, {"substeps", result.substeps}, {"milliseconds", result.milliseconds},
        {"world_unchanged", before == WorldKey(input)}, {"balls", balls},
        {"cushions", CushionJson(result.cushionCharges)},
		{"cushion_boost_consumed", result.cushionBoostConsumed},
		{"chain_impact_hits", result.chainImpactHits},
		{"stop_shield_granted", result.stopShieldGranted},
        {"player_shield_remaining", result.playerShieldRemaining},
        {"player_shield_absorbed", result.playerShieldAbsorbed},
		{"heavy_stacks", result.heavyCollisions.stackCount},
		{"heavy_stacks_gained_this_turn",
			result.heavyCollisions.gainedThisTurn},
		{"heavy_stack_distance_per_stack",
			result.heavyCollisions.distancePerStack},
        {"player_enemy_contacts", result.shot.playerEnemyContacts}, {"enemy_enemy_contacts", result.shot.enemyEnemyContacts}};
    std::filesystem::create_directories("runtime");
    std::ofstream("runtime/shot_prediction_expected.json") << data.dump(2);
}

/// <summary>
/// 検証が有効なとき、ショット後の実際のボール状態と結果を記録する。
/// </summary>
void BallShotPrediction::WriteVerificationActual(const Input& input)
{
    if (!VerificationEnabled()) return;
    nlohmann::json balls = nlohmann::json::array();
    const auto captured = Capture(input);
    for (const auto& ball : captured) balls.push_back(BallJson(ball));
    const auto completion = BossCompletion(captured);
    const auto supply = BossAdvanceSupply(captured);
    const nlohmann::json data = {{"balls", balls},
        {"boss_encounter_cleared",completion.first},
        {"boss_recovery_strong_attack_announced",BossRecoveryAnnouncement(captured)},
        {"boss_advance_supply_attempted_if_turn_continues",supply.first},
        {"boss_advance_supply_requested_if_turn_continues",supply.second},
        {"boss_minions_retired_after_shot",completion.second},
        {"cushions", CushionJson(input.cushionCharges)},
		{"cushion_boost_consumed", input.cushionBoostConsumed},
		{"heavy_stacks", input.heavyCollisions.stackCount},
		{"heavy_stacks_gained_this_turn",
			input.heavyCollisions.gainedThisTurn},
		{"heavy_stack_distance_per_stack",
			input.heavyCollisions.distancePerStack},
        {"player_enemy_contacts", input.playerEnemyContacts},
        {"enemy_enemy_contacts", input.enemyEnemyContacts}};
    std::filesystem::create_directories("runtime");
    std::ofstream("runtime/shot_prediction_actual.json") << data.dump(2);
}

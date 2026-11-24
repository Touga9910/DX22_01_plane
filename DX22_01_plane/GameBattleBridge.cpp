#include "Game.h"
#include "GamePresentation.h"
#include "AudioFeedback.h"

#include "GameRuntime.h"
#include "GameRuntimeState.h"
#include "GameWorld.h"
#include "GameObject.h"
#include "GameBattleState.h"
#include "GameBattleCommands.h"
#include "GameBattleEvents.h"
#include "GameBattleView.h"
#include "GamePlayerState.h"
#include "GameShotPrediction.h"
#include "GameShotTraceView.h"
#include "ShotRelicRules.h"
#include "CushionChargeRules.h"
#include "BallShotPrediction.h"
#include "BallComponent.h"
#include "BallFactory.h"
#include "BossPocketRules.h"
#include "BalanceLogger.h"
#include "BattleScene.h"
#include "BattleSceneServices.h"
#include "EnemyBall.h"
#include "GameDebugController.h"
#include "Pocket.h"
#include "PlayerBall.h"
#include "TableFrame.h"
#include "TableConfig.h"
#include "TableGeometry.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <random>
#include <utility>

/// <summary>
/// 現在の戦闘状態を取得する。
/// </summary>
BattleState GameBattleView::State()
{
    return Game::GetInstance()->GetBattleState();
}

/// <summary>
/// 自動プレイの状態とショット準備の条件から、プレイヤーの入力を受け付けるかを返す。
/// </summary>
bool GameBattleView::AcceptsHumanInput()
{
	Game* game = Game::GetInstance();
	return !game->IsBalanceAutoPlayEnabled() &&
		game->IsShotPreparationInputEnabled();
}

/// <summary>
/// 自動プレイが無効なデバッグモードで移動操作を受け付けるかを返す。
/// </summary>
bool GameBattleView::AcceptsDebugMovement()
{
	const Game* game = Game::GetInstance();
	return game->IsDebugMode() && !game->IsBalanceAutoPlayEnabled();
}

/// <summary>
/// クリア報酬の選択が進行中かを返す。
/// </summary>
bool GameBattleView::ClearRewardActive()
{
    return Game::GetInstance()->IsClearRewardActive();
}

/// <summary>
/// クッションを分ける領域の総数を返す。
/// </summary>
int GameBattleView::CushionRegionCount()
{
	return 12;
}

/// <summary>
/// 指定したクッション領域が範囲内で、盤面に有効なチャージが残っているかを返す。
/// </summary>
bool GameBattleView::IsCushionChargeActive(int region)
{
    if (region < 0 || region >= CushionRegionCount()) return false;
    const auto& charges = Game::GetInstance()->GetCushionCharges();
	return CushionChargeRules::TotalStacks(charges) > 0;
}

/// <summary>
/// 使用中の球の衝突攻撃力を返し、球が未設定なら既定値を返す。
/// </summary>
int GameBattleView::CurrentShotImpactAttack()
{
    const auto* ball = Game::GetInstance()->GetCurrentPlayerBallData();
    return ball != nullptr ? ball->status.impactAttack : 1;
}

/// <summary>
/// 現在のショットに適用される衝突攻撃力の加算値を返す。
/// </summary>
int GameBattleView::CurrentShotCollisionAttackBonus()
{
    return Game::GetInstance()->GetCurrentShotCollisionAttackBonus();
}

/// <summary>
/// 現在のショットで使用できる貫通の最大回数を返す。
/// </summary>
int GameBattleView::PierceMaximumUses()
{
    return Game::GetInstance()->GetPierceMaximumUses();
}

/// <summary>
/// 貫通後に維持する速度の割合を返す。
/// </summary>
float GameBattleView::PierceSpeedRetention()
{
    return Game::GetInstance()->GetPierceSpeedRetention();
}

/// <summary>
/// 戦闘中の補正を敵球同士の衝突ダメージに適用する。
/// </summary>
int GameBattleView::ScaleEnemyCollisionDamage(int damage)
{
	return Game::GetInstance()->ScaleEnemyCollisionDamage(damage);
}

/// <summary>
/// 現在使用している球が貫通カテゴリかを返す。
/// </summary>
bool GameBattleView::IsCurrentShotPierceCategory()
{
    const PlayerBallData* ball =
        Game::GetInstance()->GetCurrentPlayerBallData();
    return ball != nullptr && ball->category == BallCategory::Pierce;
}

/// <summary>
/// 現在使用している球が反射カテゴリかを返す。
/// </summary>
bool GameBattleView::IsCurrentShotBounceCategory()
{
	const PlayerBallData* ball =
		Game::GetInstance()->GetCurrentPlayerBallData();
	return ball != nullptr && ball->category == BallCategory::Bounce;
}

/// <summary>
/// 指定した対象がピンボール効果の対象として登録されているかを返す。
/// </summary>
bool GameBattleView::IsPinballTarget(std::uintptr_t targetId)
{
	return CushionChargeRules::IsPinballTarget(
		Game::GetInstance()->GetCushionCharges(),
		targetId);
}

/// <summary>
/// 敵の行動設定を戦闘コントローラーから取得し、未初期化なら既定値を返す。
/// </summary>
EnemyActionConfig Game::GetEnemyActionConfig() const
{
    return m_BattleState != nullptr
        ? m_BattleState->controller.GetEnemyActionConfig()
        : EnemyActionConfig{};
}

/// <summary>
/// 現在の貫通痕の状態を取得する。
/// </summary>
const PierceTraceRules::State& GameShotTraceView::Current()
{
    return Game::GetInstance()->GetPierceTraceState();
}

/// <summary>
/// 照準中に貫通痕を使う球が選ばれていれば、発射予定のミニオン数を算出する。
/// </summary>
std::size_t GameShotTraceView::SelectedMinionTraceCount()
{
    const Game& game = *Game::GetInstance();
    const BattleState battleState = game.GetBattleState();
    if ((battleState != BattleState::AimingDirection && battleState != BattleState::AimingPower) ||
        !game.IsShotPreparationInputEnabled())
        return 0;

    const auto* selected = game.GetBallOffer(game.GetSelectedBallOfferIndex());
    if (selected == nullptr || selected->definitionId != "player_minion_finisher")
        return 0;

    const auto& state = Current();
    const int stacks = PierceTraceRules::CalculateEffectiveMinionStackConsume(
        state, PierceTraceRules::MaxStackCount, true);
    return PierceTraceRules::MinionLaunchCount(state, stacks);
}

/// <summary>
/// 現在のステージのクリア処理をゲームに委ねる。
/// </summary>
void GameBattleCommands::CompleteCurrentStage()
{
    Game::GetInstance()->CompleteCurrentStage();
}

/// <summary>
/// 照準の方向選択に進むことをゲームに通知する。
/// </summary>
void GameBattleCommands::BeginAimDirection()
{
    Game::GetInstance()->NotifyBattleAimDirectionStarted();
}

/// <summary>
/// ショットの威力選択に進むことをゲームに通知する。
/// </summary>
void GameBattleCommands::BeginPowerSelection()
{
    Game::GetInstance()->NotifyBattlePowerSelectionStarted();
}

/// <summary>
/// ショット準備を取り消したことをゲームに通知する。
/// </summary>
void GameBattleCommands::CancelShot()
{
    Game::GetInstance()->NotifyBattleShotCancelled();
}

/// <summary>
/// バンクショットのダメージ倍率を取得し、その効果を消費する。
/// </summary>
int GameBattleCommands::ConsumeBankShotDamageMultiplier()
{
    return Game::GetInstance()->ConsumeBankShotDamageMultiplier();
}

/// <summary>
/// プレイヤー球と敵球の衝突に適用するレリックのダメージ加算値を消費する。
/// </summary>
int GameBattleCommands::ConsumePlayerEnemyRelicDamageBonus()
{
    return Game::GetInstance()->ConsumePlayerEnemyRelicDamageBonus();
}

/// <summary>
/// プレイヤー球が壁に衝突した領域、位置、反射速度をゲームに渡す。
/// </summary>
void GameBattleEvents::PlayerWallCollision(
    int cushionRegion,
    const DirectX::SimpleMath::Vector3& playerPosition,
    DirectX::SimpleMath::Vector3& reflectedVelocity)
{
    Game::GetInstance()->NotifyPlayerWallCollision(
        cushionRegion, playerPosition, reflectedVelocity);
}

/// <summary>
/// 敵球同士の衝突をゲームに伝え、連携効果によるダメージを取得する。
/// </summary>
int GameBattleEvents::EnemyEnemySynergyCollision(
    EnemyBall* first,
    EnemyBall* second,
    const DirectX::SimpleMath::Vector3& firstVelocityBefore,
    const DirectX::SimpleMath::Vector3& secondVelocityBefore)
{
	return Game::GetInstance()->NotifyEnemyEnemySynergyCollision(
        first, second, firstVelocityBefore, secondVelocityBefore);
}

/// <summary>
/// プレイヤー球と敵球の衝突をゲームに伝え、連携効果の判定結果を得る。
/// </summary>
PlayerEnemySynergyResult GameBattleEvents::PlayerEnemySynergyCollision(
	EnemyBall* enemy,
	const DirectX::SimpleMath::Vector3& enemyVelocityBefore)
{
	return Game::GetInstance()->NotifyPlayerEnemySynergyCollision(
		enemy,
		enemyVelocityBefore);
}

/// <summary>
/// 敵球が壁に衝突したときの反射前後の速度をゲームに渡す。
/// </summary>
void GameBattleEvents::EnemyWallCollision(
	EnemyBall* enemy,
	const DirectX::SimpleMath::Vector3& velocityBefore,
	DirectX::SimpleMath::Vector3& velocityAfter)
{
	Game::GetInstance()->NotifyEnemyWallCollision(
		enemy,
		velocityBefore,
		velocityAfter);
}

/// <summary>
/// 壁に衝突した位置、接近速度、対象をゲームに通知する。
/// </summary>
void GameBattleEvents::WallImpact(
	const DirectX::SimpleMath::Vector3& worldPosition,
	float relativeApproachSpeed,
	const void* target)
{
	Game::GetInstance()->NotifyWallImpact(
		worldPosition,
		relativeApproachSpeed,
		target);
}

/// <summary>
/// 敵の能力状態が変化したことをゲームに通知する。
/// </summary>
void GameBattleEvents::EnemyAbilityStateChanged(
	const EnemyAbilityStateChangedEvent& event)
{
	Game::GetInstance()->NotifyEnemyAbilityStateChanged(event);
}

/// <summary>
/// プレイヤー球が敵球を貫通した位置と方向をゲームに通知する。
/// </summary>
void GameBattleEvents::PlayerPiercedEnemy(
    EnemyBall* enemy,
    const DirectX::SimpleMath::Vector3& playerPosition,
	const DirectX::SimpleMath::Vector3& direction)
{
    Game::GetInstance()->NotifyPlayerPiercedEnemy(
		enemy, playerPosition, direction);
}

/// <summary>
/// プレイヤー球の進行方向が変わった位置をゲームに通知する。
/// </summary>
void GameBattleEvents::PlayerDirectionChange(
    const DirectX::SimpleMath::Vector3& playerPosition)
{
    Game::GetInstance()->NotifyPlayerDirectionChange(playerPosition);
}

/// <summary>
/// 直接衝突した敵を起点とする連鎖攻撃をゲームに通知する。
/// </summary>
void GameBattleEvents::PlayerChainImpact(
    EnemyBall* directTarget,
    int attackDamage,
    float radius)
{
    Game::GetInstance()->NotifyPlayerChainImpact(
        directTarget, attackDamage, radius);
}

/// <summary>
/// 指定範囲への連鎖攻撃を中心座標と除外対象を添えてゲームに通知する。
/// </summary>
void GameBattleEvents::PlayerChainImpact(
    const DirectX::SimpleMath::Vector3& center,
    const EnemyBall* excludedTarget,
    int attackDamage,
    float radius)
{
    Game::GetInstance()->NotifyPlayerChainImpact(
        center, excludedTarget, attackDamage, radius);
}

/// <summary>
/// アンカーで球が停止したことと直接衝突した対象をゲームに通知する。
/// </summary>
void GameBattleEvents::AnchorStopped(EnemyBall* directTarget)
{
    Game::GetInstance()->NotifyAnchorStopped(directTarget);
}

/// <summary>
/// ダメージを伴う球の衝突種別をゲームに通知する。
/// </summary>
void GameBattleEvents::DamageBallCollision(
    DamageBallCollisionType collisionType)
{
    Game::GetInstance()->NotifyDamageBallCollision(collisionType);
}

/// <summary>
/// 敵に与えたダメージの位置、数値、撃破結果を演出用にゲームへ渡す。
/// </summary>
void GameBattleEvents::CombatFeedback(
    const DirectX::SimpleMath::Vector3& worldPosition,
    int damage,
    bool defeated,
	bool enemyEnemyCollision,
	const EnemyBall* target)
{
    Game::GetInstance()->NotifyCombatFeedback(
		worldPosition, damage, defeated, enemyEnemyCollision, target);
}

/// <summary>
/// 衝突した球の位置、接近速度、ダメージなどを衝突演出のためにゲームへ渡す。
/// </summary>
void GameBattleEvents::CollisionImpact(
    const DirectX::SimpleMath::Vector3& worldPosition,
    const DirectX::SimpleMath::Vector3& firstTargetPosition,
    const DirectX::SimpleMath::Vector3& secondTargetPosition,
    float relativeApproachSpeed,
    CombatCollisionKind collisionKind,
    int damage,
    bool defeated,
    const EnemyBall* firstTarget,
    const EnemyBall* secondTarget)
{
    Game::GetInstance()->NotifyCollisionImpact(
        worldPosition,
        firstTargetPosition,
        secondTargetPosition,
        relativeApproachSpeed,
        collisionKind,
        damage,
        defeated,
        firstTarget,
        secondTarget);
}

/// <summary>
/// 無傷の敵が残ったことを自動プレイの判定へ伝える。
/// </summary>
void GameBattleEvents::BalanceAutoFullHpEnemySurvived()
{
    Game::GetInstance()->NotifyBalanceAutoFullHpEnemySurvived();
}

/// <summary>
/// 敵球がポケットに入ったときの処理をゲームに委ねる。
/// </summary>
void GameBattleEvents::EnemyPocketed(EnemyBall* enemy)
{
    Game::GetInstance()->HandleEnemyPocket(enemy);
}

/// <summary>
/// 敵球が撃破されたことをゲームに通知する。
/// </summary>
void GameBattleEvents::EnemyDefeated(EnemyBall* enemy)
{
    Game::GetInstance()->NotifyEnemyDefeated(enemy);
}

/// <summary>
/// プレイヤーが敗北したことをゲームの戦闘処理に通知する。
/// </summary>
void GameBattleEvents::PlayerDefeated()
{
    Game::GetInstance()->NotifyBattlePlayerDefeated();
}

/// <summary>
/// プレイヤー球のショットが行われたことをゲームに通知する。
/// </summary>
void GameBattleEvents::PlayerShotFired(PlayerBall* player)
{
    Game::GetInstance()->OnPlayerShotFired(player);
}

/// <summary>
/// プレイヤーが受けたダメージと前後のHPをゲームに通知する。
/// </summary>
void GameBattleEvents::PlayerDamage(
    const std::string& source,
    int damage,
    const std::string& enemyId,
    int hpBefore,
    int hpAfter)
{
    Game::GetInstance()->NotifyPlayerDamage(
        source, damage, enemyId, hpBefore, hpAfter);
}

/// <summary>
/// プレイヤー球のポケット時の位置とダメージを演出用にゲームへ渡す。
/// </summary>
void GameBattleEvents::PlayerPocketFeedback(
    const DirectX::SimpleMath::Vector3& worldPosition,
    int damage)
{
    Game::GetInstance()->NotifyPocketFeedback(
        worldPosition, true, false, damage);
}

/// <summary>
/// プレイヤーの現在の能力値を指定した球に反映する。
/// </summary>
void GamePlayerState::ApplyTo(PlayerBall* player)
{
    Game::GetInstance()->ApplyPlayerStatusTo(player);
}

/// <summary>
/// 指定したプレイヤー球から現在の能力値を取得して保持する。
/// </summary>
void GamePlayerState::CaptureFrom(const PlayerBall* player)
{
    Game::GetInstance()->CapturePlayerStatusFrom(player);
}

/// <summary>
/// プレイヤー球がポケットに入った場合のダメージを取得する。
/// </summary>
int GamePlayerState::PocketDamageAmount()
{
    return Game::GetInstance()->GetPlayerPocketDamageAmount();
}

/// <summary>
/// 受けたダメージをシールドで吸収し、残りのHPダメージを返す。
/// </summary>
int GamePlayerState::AbsorbShieldDamage(int damage)
{
    return Game::GetInstance()->AbsorbPlayerShieldDamage(damage);
}

namespace
{
/// <summary>
/// 現在の盤面と貫通設定から、軌道プレビューへの入力を作成する。
/// </summary>
BallTrajectoryPreview::Input MakeTrajectoryPreviewInput(
	const PlayerBall& player)
{
	Game& game = *Game::GetInstance();
	return BallTrajectoryPreview::Capture(
		GameRuntime::World(),
		*player.GetBall(),
		game.GetPierceMaximumUses(),
		game.GetPierceSpeedRetention());
}

/// <summary>
/// 盤面の球・台枠・ポケットと戦闘設定を集め、ショット予測用の入力を作成する。
/// </summary>
BallShotPrediction::Input MakeShotPredictionInput(float launchPower)
{
    Game& game = *Game::GetInstance();
    BallShotPrediction::Input input;
    input.balls = GameRuntime::World().GetComponents<BallComponent>();
    input.tableFrames = GameRuntime::World().GetComponents<TableFrame>();
    input.pockets = GameRuntime::World().GetComponents<Pocket>();
    input.cushionCharges = game.GetCushionCharges();
    input.pierceTraces = game.GetPierceTraceState();
    input.shot = game.MakePredictionShotRules(launchPower);
    if (const PlayerBallData* currentBall = game.GetCurrentPlayerBallData())
    {
        input.hasCurrentBall = true;
        input.currentBallCategory = static_cast<int>(currentBall->category);
    }
    input.playerPocketDamage = game.GetPlayerPocketDamageAmount();
    input.pierceMaximumUses = game.GetPierceMaximumUses();
    input.pierceSpeedRetention = game.GetPierceSpeedRetention();
    input.hasPierceBallCharger =
        game.HasRelic(RelicType::PierceBallCharger);
    input.relicAttackBonus = game.GetRelicAttackBonus();
	input.heavyCollisions = game.GetHeavyCollisionState();
    input.playerAnchorStacks = game.GetPlayerAnchorStacks();
	input.anchorConversionProgress = game.GetAnchorConversionProgress();
    input.anchorNextGrantBonus = game.GetAnchorStackState().nextAnchorGrantBonus;
    input.cushionBoostConsumed = game.WasCushionBoostConsumedThisShot();
    input.playerEnemyContacts =
        game.GetCurrentShotPlayerEnemyCollisionCount();
    input.enemyEnemyContacts =
        game.GetCurrentShotEnemyEnemyCollisionCount();
    return input;
}
}

/// <summary>
/// 指定した速度と初回の摩擦条件をもとにプレイヤー球の軌道を予測する。
/// </summary>
BallTrajectoryPreview::Result GameShotPrediction::Preview(
	const PlayerBall& player,
	const DirectX::SimpleMath::Vector3& velocity,
	bool skipFirstPlayerFriction)
{
	return BallTrajectoryPreview::Predict(
		MakeTrajectoryPreviewInput(player),
		velocity,
		skipFirstPlayerFriction);
}

/// <summary>
/// 軌道プレビューに影響する盤面と貫通設定の識別キーを取得する。
/// </summary>
std::uint64_t GameShotPrediction::PreviewWorldKey()
{
	Game& game = *Game::GetInstance();
	return BallTrajectoryPreview::WorldKey(
		GameRuntime::World(),
		game.GetPierceMaximumUses(),
		game.GetPierceSpeedRetention());
}

/// <summary>
/// 現在の戦闘状態を使い、指定した速度と球候補によるショット結果を予測する。
/// </summary>
BallShotPrediction::Result GameShotPrediction::Predict(
    const PlayerBall& player,
    const DirectX::SimpleMath::Vector3& velocity,
    bool skipFirstPlayerFriction,
    const PlayerBallData* offer)
{
    const auto input = MakeShotPredictionInput(velocity.Length());
    return BallShotPrediction::Predict(
        input, player, velocity, skipFirstPlayerFriction, offer);
}

/// <summary>
/// ショット予測に影響する盤面と設定から識別キーを取得する。
/// </summary>
std::uint64_t GameShotPrediction::WorldKey()
{
    return BallShotPrediction::WorldKey(MakeShotPredictionInput(0.0f));
}

/// <summary>
/// 指定したショットの予測結果を検証用に記録する。
/// </summary>
void GameShotPrediction::WriteVerification(
    const PlayerBall& player,
    const DirectX::SimpleMath::Vector3& velocity,
    bool skipFirstPlayerFriction)
{
    const auto input = MakeShotPredictionInput(velocity.Length());
    BallShotPrediction::WriteVerificationPrediction(
        input, player, velocity, skipFirstPlayerFriction);
}

/// <summary>
/// 実際の盤面とショット結果を予測との比較用に記録する。
/// </summary>
void GameShotPrediction::WriteVerificationActual()
{
    BallShotPrediction::WriteVerificationActual(
        MakeShotPredictionInput(0.0f));
}

/// <summary>
/// 戦闘コントローラーから現在の進行状態を取得する。
/// </summary>
BattleState Game::GetBattleState() const
{
    return m_BattleState->controller.GetState();
}

/// <summary>
/// 戦闘コントローラーがすべての球の停止を検知しているかを返す。
/// </summary>
bool Game::AreAllBallsStopped() const
{
    return m_BattleState->controller.AreAllBallsStopped();
}

/// <summary>
/// 診断用の戦闘コントローラーを取得し、未初期化ならヌルポインタを返す。
/// </summary>
const BattleController* Game::GetBattleControllerDiagnostics() const
{
    return m_BattleState != nullptr ? &m_BattleState->controller : nullptr;
}

/// <summary>
/// クッションのチャージ状態を取得する。
/// </summary>
const CushionChargeRules::State& Game::GetCushionCharges() const
{
    return m_BattleState->cushionCharges;
}

/// <summary>
/// 現在のショットでクッション強化をすでに消費したかを返す。
/// </summary>
bool Game::WasCushionBoostConsumedThisShot() const
{
    return m_BattleState->cushionBoostConsumedThisShot;
}

/// <summary>
/// 戦闘中に蓄積した貫通痕の状態を取得する。
/// </summary>
const PierceTraceRules::State& Game::GetPierceTraceState() const
{
    return m_BattleState->pierceTraces;
}

/// <summary>
/// 現在の重衝突スタックの数を返す。
/// </summary>
int Game::GetHeavyCollisionCount() const
{
	return m_BattleState->heavyCollisions.stackCount;
}

/// <summary>
/// 戦闘中の重衝突効果の状態を取得する。
/// </summary>
const HeavyCollisionRules::State& Game::GetHeavyCollisionState() const
{
	return m_BattleState->heavyCollisions;
}

/// <summary>
/// プレイヤーが保持しているアンカースタックの数を返す。
/// </summary>
int Game::GetPlayerAnchorStacks() const
{
    return m_BattleState->anchorStacks.playerStacks;
}

/// <summary>
/// アンカースタック変換の進行度を返す。
/// </summary>
int Game::GetAnchorConversionProgress() const
{
	return m_BattleState->anchorStacks.conversionProgress;
}

/// <summary>
/// 戦闘中のアンカースタックの状態を取得する。
/// </summary>
const AnchorStackRules::State& Game::GetAnchorStackState() const
{
	return m_BattleState->anchorStacks;
}

/// <summary>
/// 現在のショットで強化されたクッション効果を使った回数を返す。
/// </summary>
int Game::GetCushionStrongUsesThisShot() const
{
    return m_BattleState->cushionStrongUsesThisShot;
}

/// <summary>
/// プレイヤーの現在のシールド値を返す。
/// </summary>
int Game::GetPlayerShield() const
{
    return m_BattleState->playerShield;
}

/// <summary>
/// 現在のショットでレリックが加える衝突攻撃力を取得する。
/// </summary>
int Game::GetCurrentShotCollisionAttackBonus() const
{
    return m_BattleState->controller.GetShotRelicRules().collisionBonus;
}

/// <summary>
/// 現在のショットでプレイヤー球が敵球に接触した回数を返す。
/// </summary>
int Game::GetCurrentShotPlayerEnemyCollisionCount() const
{
    return m_BattleState->controller.GetShotRelicRules().playerEnemyContacts;
}

/// <summary>
/// 現在のショットで敵球同士が接触した回数を返す。
/// </summary>
int Game::GetCurrentShotEnemyEnemyCollisionCount() const
{
    return m_BattleState->controller.GetShotRelicRules().enemyEnemyContacts;
}

/// <summary>
/// 現在のショットで発生したプレイヤー球と敵球および敵球同士の接触回数を合計する。
/// </summary>
int Game::GetCurrentShotBallCollisionCount() const
{
    const auto& rules = m_BattleState->controller.GetShotRelicRules();
    return rules.playerEnemyContacts + rules.enemyEnemyContacts;
}

/// <summary>
/// 現在のショットでバンクショット効果を使用できるかを返す。
/// </summary>
bool Game::IsCurrentShotBankShotReady() const
{
    const auto& rules = m_BattleState->controller.GetShotRelicRules();
    return rules.bankReady && !rules.bankConsumed;
}

namespace
{
class GameBattleSceneServices final : public BattleSceneServices
{
public:
	/// <summary>
	/// 戦闘シーンに提供する操作の呼び出し先としてゲームを保持する。
	/// </summary>
	explicit GameBattleSceneServices(Game& game)
		: m_Game(game)
	{
	}

	/// <summary>
	/// 戦闘シーンが参照するゲームを取得する。
	/// </summary>
	Game& GameContext() const override
	{
		return m_Game;
	}

	/// <summary>
	/// 通常の選択を上書きするステージデータを取得する。
	/// </summary>
	const StageData* CurrentStageOverride() const override
	{
		return m_Game.GetCurrentStageOverride();
	}

	/// <summary>
	/// 選択されているステージのIDを取得する。
	/// </summary>
	const std::string& SelectedStageId() const override
	{
		return m_Game.GetSelectedStageId();
	}

	/// <summary>
	/// 現在の難度設定に基づく補正を敵データへ適用する。
	/// </summary>
	void ApplyEnemyDifficultyScaling(EnemyData& enemyData) const override
	{
		m_Game.ApplyEnemyDifficultyScaling(enemyData);
	}

	/// <summary>
	/// プレイヤー球にデバッグ戦闘の設定を適用する。
	/// </summary>
	void ApplyDebugPlayer(PlayerBall* player) const override
	{
		m_Game.ApplyDebugBattlePlayer(player);
	}

	/// <summary>
	/// 指定位置の敵球にデバッグ戦闘の設定を適用する。
	/// </summary>
	void ApplyDebugEnemy(
		EnemyBall* enemy,
		std::size_t index) const override
	{
		m_Game.ApplyDebugBattleEnemy(enemy, index);
	}

	/// <summary>
	/// ステージが開始したこととそのステージデータをゲームに通知する。
	/// </summary>
	void NotifyStageStarted(const StageData& stage) const override
	{
		m_Game.OnBattleStageStarted(stage);
	}

	/// <summary>
	/// ボス戦開始時の配置で初期ミニオンを生成する。
	/// </summary>
	std::vector<EnemyBall*> SpawnInitialBossMinions() const override
	{
		return m_Game.SpawnBossMinions(BossEncounterRules::InitialMinions, true);
	}

	/// <summary>
	/// ゲームがデバッグモードで動作しているかを返す。
	/// </summary>
	bool IsDebugMode() const override
	{
		return m_Game.IsDebugMode();
	}

	/// <summary>
	/// デバッグ戦闘でブレイク用に指定された球の配置を取得する。
	/// </summary>
	const std::vector<DirectX::SimpleMath::Vector3>&
	DebugBreakBallPositions() const override
	{
		return m_Game.GetDebugBreakBallPositions();
	}

	/// <summary>
	/// 現在のワールドに存在する敵球を取得する。
	/// </summary>
	std::vector<EnemyBall*> Enemies() const override
	{
		return GameRuntime::World().GetComponents<EnemyBall>();
	}

	/// <summary>
	/// 指定した理由で戦闘の予測値を再計算するよう要求する。
	/// </summary>
	void InvalidateCombatForecast(const char* reason) const override
	{
		m_Game.InvalidateDebugCombatForecast(reason);
	}

private:
	Game& m_Game;
};
}

/// <summary>
/// ゲームの操作を戦闘シーンに渡すシーン生成処理を登録する。
/// </summary>
void Game::InitializeBattleSceneFactory()
{
	m_RuntimeState->SetBattleSceneFactory(
		[this]() -> std::unique_ptr<Scene>
		{
			auto services =
				std::make_unique<GameBattleSceneServices>(*this);
			return std::make_unique<BattleScene>(
				std::move(services));
		});
}

// BattleControllerがGame全体へ依存しないよう、
// 必要な処理だけをHookとして接続する。
/// <summary>
/// ショット、敵の行動、戦闘結果などを処理するコールバックを戦闘コントローラーに登録する。
/// </summary>
void Game::InitializeBattleController()
{
	BattleControllerHooks hooks{};
    hooks.playerShield = [this]() { return GetPlayerShield(); };
    hooks.enemyAttackLaunched = [this](const EnemyBall& enemy, const PlayerBall& player)
    {
        if (m_GamePresentation && !IsBalanceAutoPlayEnabled())
            m_GamePresentation->OnEnemyAttackLaunched(enemy.GetPosition(), player.GetPosition());
    };
    hooks.enemyAttackImpact = [this](int hpDamage, int absorbed)
    {
        if (m_GamePresentation && !IsBalanceAutoPlayEnabled())
            m_GamePresentation->OnEnemyAttackImpact(hpDamage, absorbed);
        if (m_AudioFeedback) m_AudioFeedback->OnPlayerImpact(hpDamage, absorbed);
    };

	hooks.restorePocketedPlayer =
		[this]()
		{
			RestorePocketedPlayer();
		};

	hooks.applyEndOfShotEffects =
		[this](PlayerBall* player)
		{
			ApplyEndOfShotRelicEffects(player);
		};

	hooks.endShotLog =
		[](int playerHp, int aliveEnemies, int defeatedEnemies)
		{
			BalanceLogger::GetInstance().EndShot(
				playerHp,
				aliveEnemies,
				defeatedEnemies);
		};

	hooks.beginEnemyAttackForecast =
		[this]() -> int
		{
			return m_DebugController->BeginEnemyAttackForecast(*this);
		};

	hooks.endEnemyAttackForecast =
		[this](int predictedDamage, int actualDamage)
		{
			m_DebugController->EndEnemyAttackForecast(
				predictedDamage,
				actualDamage);
		};

	hooks.notifyPlayerDamage =
		[this](
			const std::string& source,
			int damage,
			const std::string& sourceId,
			int hpBefore,
			int hpAfter)
		{
			NotifyPlayerDamage(
				source,
				damage,
				sourceId,
				hpBefore,
				hpAfter);
		};

	hooks.capturePlayerStatus =
		[this](PlayerBall* player)
		{
			CapturePlayerStatusFrom(player);
		};

    hooks.spawnBossMinions = [this](int count, bool advance)
    {
        const auto spawned = SpawnBossMinions(count,false,advance);
        if (auto* scene = dynamic_cast<BattleScene*>(GetCurrentScene()))
            for (auto* enemy : spawned) scene->TrackSpawnedObject(enemy->GetGameObject());
        RecordBalanceEvent("boss_minions_summoned", {{"requested",count},{"spawned",spawned.size()},{"advance",advance}});
    };
	hooks.spawnPhaseEnemies =
		[this](
			const std::vector<EnemySpawnData>& spawns,
			int defaultInterval,
			unsigned int spawnSeed,
			int phaseNumber)
		{
			RecordBalanceEvent("phase_ended", {
				{ "phase", phaseNumber - 1 },
				{ "total_phases",
					m_BattleState->controller.GetTotalPhases() },
				{ "reason", "reinforcement_advance" },
				{ "living_enemy_count_before_reinforcement",
					m_BattleState->controller.GetLivingEnemyCount() },
			});
			std::mt19937 random(spawnSeed +
				static_cast<unsigned int>(phaseNumber));
			using Vector3 = DirectX::SimpleMath::Vector3;
			using PendingBall = std::pair<Vector3, float>;
			auto isClear = [this](
				const EnemySpawnData& spawn,
				const Vector3& candidate,
				const std::vector<PendingBall>& pending)
			{
				const float radius = (std::max)(
					0.01f, spawn.enemyData.status.radius);
				const float halfWidth =
					TableConfig::GetFieldWidth() * 0.5f - radius - 1.0f;
				const float halfDepth =
					TableConfig::GetFieldDepth() * 0.5f - radius - 1.0f;
				if (std::abs(candidate.x) > halfWidth ||
					std::abs(candidate.z) > halfDepth)
				{
					return false;
				}
				for (BallComponent* ball :
					m_RuntimeState->World().GetComponents<BallComponent>())
				{
					if (ball == nullptr || ball->GetGameObject() == nullptr ||
						!ball->GetGameObject()->IsActive()) continue;
					Vector3 delta = candidate - ball->GetPosition();
					delta.y = 0.0f;
					const float clearance =
						radius + ball->GetRadius() + 1.0f;
					if (delta.LengthSquared() < clearance * clearance)
						return false;
				}
				for (const auto& pocket : TableConfig::GetPocketCenters())
				{
					Vector3 delta = candidate - pocket;
					delta.y = 0.0f;
					const float clearance = radius + 5.0f;
					if (delta.LengthSquared() < clearance * clearance)
						return false;
				}
				for (const PendingBall& other : pending)
				{
					Vector3 delta = candidate - other.first;
					delta.y = 0.0f;
					const float clearance = radius + other.second + 1.0f;
					if (delta.LengthSquared() < clearance * clearance)
						return false;
				}
				return true;
			};
			auto resolveIndividualPosition = [&](const EnemySpawnData& spawn)
			{
				const float radius = (std::max)(
					0.01f, spawn.enemyData.status.radius);
				const float halfWidth =
					TableConfig::GetFieldWidth() * 0.5f - radius - 1.0f;
				const float halfDepth =
					TableConfig::GetFieldDepth() * 0.5f - radius - 1.0f;
				std::uniform_real_distribution<float> xDistribution(
					-halfWidth, halfWidth);
				std::uniform_real_distribution<float> zDistribution(
					-halfDepth, halfDepth);
				Vector3 position = spawn.position;
				position.y = TableConfig::FIELD_HEIGHT;
				const std::vector<PendingBall> none;
				for (int attempt = 0;
					attempt < 64 && !isClear(spawn, position, none); ++attempt)
				{
					position = Vector3(
						xDistribution(random), TableConfig::FIELD_HEIGHT,
						zDistribution(random));
				}
				return position;
			};
			auto spawnAt = [this, defaultInterval](
				const EnemySpawnData& spawn, const Vector3& position)
			{
				EnemyData data = spawn.enemyData;
				data.initPosition = position;
				EnemyBall* enemy = BallFactory::CreateEnemy(*this, data);
				if (BattleScene* scene =
					dynamic_cast<BattleScene*>(GetCurrentScene()))
				{
					scene->TrackSpawnedObject(enemy->GetGameObject());
				}
				enemy->InitializeAttackTimer(
					spawn.attackInterval > 0
						? spawn.attackInterval
						: defaultInterval);
			};

			std::vector<bool> handled(spawns.size(), false);
			for (std::size_t spawnIndex = 0;
				spawnIndex < spawns.size(); ++spawnIndex)
			{
				if (handled[spawnIndex]) continue;
				const EnemySpawnData& first = spawns[spawnIndex];
				if (first.spawnGroup <= 0)
				{
					spawnAt(first, resolveIndividualPosition(first));
					handled[spawnIndex] = true;
					continue;
				}

				std::vector<std::size_t> groupIndices;
				for (std::size_t candidateIndex = spawnIndex;
					candidateIndex < spawns.size(); ++candidateIndex)
				{
					if (!handled[candidateIndex] &&
						spawns[candidateIndex].spawnGroup == first.spawnGroup)
					{
						groupIndices.push_back(candidateIndex);
					}
				}

				float minimumOffsetX = -TableConfig::GetFieldWidth();
				float maximumOffsetX = TableConfig::GetFieldWidth();
				float minimumOffsetZ = -TableConfig::GetFieldDepth();
				float maximumOffsetZ = TableConfig::GetFieldDepth();
				for (std::size_t index : groupIndices)
				{
					const EnemySpawnData& spawn = spawns[index];
					const float radius = (std::max)(
						0.01f, spawn.enemyData.status.radius);
					const float halfWidth = TableConfig::GetFieldWidth() * 0.5f -
						radius - 1.0f;
					const float halfDepth = TableConfig::GetFieldDepth() * 0.5f -
						radius - 1.0f;
					minimumOffsetX = (std::max)(
						minimumOffsetX, -halfWidth - spawn.position.x);
					maximumOffsetX = (std::min)(
						maximumOffsetX, halfWidth - spawn.position.x);
					minimumOffsetZ = (std::max)(
						minimumOffsetZ, -halfDepth - spawn.position.z);
					maximumOffsetZ = (std::min)(
						maximumOffsetZ, halfDepth - spawn.position.z);
				}

				std::vector<Vector3> resolvedGroup;
				auto tryGroupOffset = [&](float offsetX, float offsetZ)
				{
					std::vector<PendingBall> pending;
					std::vector<Vector3> candidates;
					for (std::size_t index : groupIndices)
					{
						const EnemySpawnData& spawn = spawns[index];
						Vector3 candidate = spawn.position +
							Vector3(offsetX, 0.0f, offsetZ);
						candidate.y = TableConfig::FIELD_HEIGHT;
						if (!isClear(spawn, candidate, pending)) return false;
						candidates.push_back(candidate);
						pending.emplace_back(candidate, (std::max)(
							0.01f, spawn.enemyData.status.radius));
					}
					resolvedGroup = std::move(candidates);
					return true;
				};

				bool groupPlaced = tryGroupOffset(0.0f, 0.0f);
				constexpr float kPi = 3.14159265358979323846f;
				for (int attempt = 1; attempt <= 48 && !groupPlaced; ++attempt)
				{
					const int ring = (attempt + 7) / 8;
					const float angle = static_cast<float>((attempt - 1) % 8) *
						(kPi * 0.25f);
					const float distance = static_cast<float>(ring) * 3.0f;
					const float offsetX = std::cos(angle) * distance;
					const float offsetZ = std::sin(angle) * distance;
					if (offsetX < minimumOffsetX || offsetX > maximumOffsetX ||
						offsetZ < minimumOffsetZ || offsetZ > maximumOffsetZ)
					{
						continue;
					}
					groupPlaced = tryGroupOffset(offsetX, offsetZ);
				}
				if (!groupPlaced && minimumOffsetX <= maximumOffsetX &&
					minimumOffsetZ <= maximumOffsetZ)
				{
					std::uniform_real_distribution<float> xOffsetDistribution(
						minimumOffsetX, maximumOffsetX);
					std::uniform_real_distribution<float> zOffsetDistribution(
						minimumOffsetZ, maximumOffsetZ);
					for (int attempt = 0; attempt < 128 && !groupPlaced; ++attempt)
					{
						groupPlaced = tryGroupOffset(
							xOffsetDistribution(random), zOffsetDistribution(random));
					}
				}

				for (std::size_t groupIndex = 0;
					groupIndex < groupIndices.size(); ++groupIndex)
				{
					const std::size_t index = groupIndices[groupIndex];
					spawnAt(spawns[index], groupPlaced
						? resolvedGroup[groupIndex]
						: resolveIndividualPosition(spawns[index]));
					handled[index] = true;
				}
			}
			for (EnemyBall* enemy :
				m_RuntimeState->World().GetComponents<EnemyBall>())
			{
				if (enemy != nullptr && enemy->CanParticipateInCombat())
				{
					enemy->RefreshBoardAbilityState(true);
				}
			}
			InvalidateDebugCombatForecast("フェーズ増援生成");
			RecordBalanceEvent("phase_transition", {
				{ "phase", phaseNumber },
				{ "total_phases", m_BattleState->controller.GetTotalPhases() },
				{ "spawned_enemy_count", static_cast<int>(spawns.size()) },
				{ "living_enemy_count",
					m_BattleState->controller.GetLivingEnemyCount() },
				{ "spawn_seed", spawnSeed },
			});
			RecordBalanceEvent("phase_started", {
				{ "phase", phaseNumber },
				{ "total_phases",
					m_BattleState->controller.GetTotalPhases() },
				{ "spawned_enemy_count", static_cast<int>(spawns.size()) },
				{ "living_enemy_count",
					m_BattleState->controller.GetLivingEnemyCount() },
			});
		};

	hooks.enemyActionPlanConfirmed =
		[this](const EnemyActionTurnPlan& plan)
		{
			nlohmann::json actions = nlohmann::json::array();
			int normalCount = 0;
			int chargeStartCount = 0;
			int chargeAttackCount = 0;
			for (const PlannedEnemyAction& assignment : plan.actions)
			{
				const EnemyBall* enemy =
					reinterpret_cast<const EnemyBall*>(assignment.id);
			const auto enemies =
				m_RuntimeState->World().GetComponents<EnemyBall>();
			const auto found = std::find(enemies.begin(), enemies.end(), enemy);
			const std::string targetId = found != enemies.end()
				? "enemy:" + std::to_string(
					static_cast<std::size_t>(
						std::distance(enemies.begin(), found)))
				: std::string();
				actions.push_back({
					{ "enemy_id", enemy != nullptr
						? enemy->GetEnemyId()
						: "unknown" },
				{ "target_id", targetId },
					{ "action", ToString(assignment.action) },
					{ "cost", assignment.cost },
				{ "predicted_damage_before_defense",
					enemy != nullptr &&
						(assignment.action == EnemyActionType::NormalAttack ||
						 assignment.action == EnemyActionType::ChargeAttack)
						? PlayerBall::CalculateIncomingDamage(
							static_cast<int>(std::lround(
								enemy->GetAttack() *
								enemy->GetPlannedDamageMultiplier())))
						: 0 },
				});
				if (assignment.action == EnemyActionType::NormalAttack)
					++normalCount;
				else if (assignment.action == EnemyActionType::ChargeStart)
					++chargeStartCount;
				else if (assignment.action == EnemyActionType::ChargeAttack)
					++chargeAttackCount;
			}
			RecordBalanceEvent("enemy_action_plan", {
				{ "budget_limit", plan.budgetLimit },
				{ "budget_spent", plan.budgetSpent },
				{ "reserved_next_turn", plan.reservedChargeAttackBudget },
				{ "normal_attack_count", normalCount },
				{ "charge_start_count", chargeStartCount },
				{ "charge_attack_count", chargeAttackCount },
				{ "actions", std::move(actions) },
			});
			InvalidateDebugCombatForecast("敵行動計画確定");
		};

	hooks.chargeInterrupted =
		[this](
			const EnemyBall& enemy,
			const ChargeInterruptionResult& result)
		{
			NotifyChargeInterrupted(enemy, result);
		};

	hooks.prepareNextTurn =
		[this]() -> bool
		{
			return PrepareNextPlayerBall();
		};

	hooks.onClearStateRecovered =
		[this](
			const char* source,
			BattleState previousState)
		{
			RecordBalanceEvent(
				"battle_clear_state_recovered",
				{
					{
						"source",
						source != nullptr
							? source
							: "unknown"
					},
					{
						"previous_battle_state",
						ToString(previousState)
					},
				});
		};

	m_BattleState->controller.Initialize(
		m_RuntimeState->World(),
		std::move(hooks));
}

// PlayerBall側から戦闘状態の変化を通知する。
/// <summary>
/// 戦闘コントローラーを方向選択状態に進める。
/// </summary>
void Game::NotifyBattleAimDirectionStarted()
{
	m_BattleState->controller.BeginAimingDirection();
}

/// <summary>
/// 戦闘コントローラーを威力選択状態に進める。
/// </summary>
void Game::NotifyBattlePowerSelectionStarted()
{
	m_BattleState->controller.BeginAimingPower();
}

/// <summary>
/// ショット選択の取り消しを受けて方向選択に戻る。
/// </summary>
void Game::NotifyBattleShotCancelled()
{
	m_BattleState->controller.BeginAimingDirection();
}

/// <summary>
/// プレイヤーの敗北を戦闘コントローラーに通知する。
/// </summary>
void Game::NotifyBattlePlayerDefeated()
{
	m_BattleState->controller.NotifyPlayerDefeated();
}

/// <summary>
/// 戦闘中のボスを取得し、配置条件と生存数の上限に応じてミニオンを生成する。
/// </summary>
std::vector<EnemyBall*> Game::SpawnBossMinions(int count, bool initialFormation, bool advanceFormation)
{
    using DirectX::SimpleMath::Vector3;
    EnemyBall* boss = nullptr;
    int living = 0;
    for (auto* enemy : GameRuntime::World().GetComponents<EnemyBall>())
    {
        if (enemy->IsArmorBoss() && enemy->CanParticipateInCombat()) boss = enemy;
        if (enemy->IsBossMinion() && enemy->CanParticipateInCombat()) ++living;
    }
    std::vector<EnemyBall*> spawned;
    if (!boss) return spawned;
    count = (std::min)(count, BossEncounterRules::MaximumMinions-living);
    std::vector<Collision::Sphere> pockets;
    for (auto* pocket : GameRuntime::World().GetComponents<Pocket>()) pockets.push_back(pocket->GetSphere());
    const Vector3 preferred[] = {{-3,1,7},{20,1,6},{-20,1,12},{-28,1,-6},{28,1,22}};
    for (int i=0;i<count;++i)
    {
        std::vector<BossPocketRules::Obstacle> obstacles;
        for (auto* ball : GameRuntime::World().GetComponents<BallComponent>())
            if (ball->GetGameObject()->IsActive()) obstacles.push_back({ball->GetPosition(),ball->GetRadius()});
        const auto slot = BossEncounterRules::InitialMinionPositions[i % BossEncounterRules::InitialMinions];
        const auto advanceSlot = BossEncounterRules::AdvanceMinionPositions[boss->BossTurn().nextMinionSerial%2];
        const auto position = initialFormation
            ? BossPocketRules::FindSpawnPosition(Vector3(slot.x,TableConfig::FIELD_HEIGHT,slot.z),
                BossEncounterRules::MinionRadius,obstacles,pockets)
            : advanceFormation ? BossPocketRules::FindSpawnPosition(Vector3(advanceSlot.x,TableConfig::FIELD_HEIGHT,advanceSlot.z),
                BossEncounterRules::MinionRadius,obstacles,pockets)
            : BossPocketRules::FindReturnPosition(preferred[boss->BossTurn().nextMinionSerial%5],
                BossEncounterRules::MinionRadius,obstacles,pockets);
        if (!position) break;
        EnemyData data;
        data.id = BossEncounterRules::MinionId;
        data.maxHp = BossEncounterRules::MinionHp;
        data.status.attack = BossEncounterRules::MinionAttack;
        data.status.defense = 0;
        data.status.mass = BossEncounterRules::MinionMass;
        data.status.radius = BossEncounterRules::MinionRadius;
        data.status.restitution = 0.8f;
        data.status.friction = 0.02f;
        data.scale = Vector3(BossEncounterRules::MinionRadius);
        data.initPosition = *position;
        data.rewardMoney = data.rewardExp = 0;
        auto* minion = BallFactory::CreateEnemy(*this,data);
        minion->SetBossMinionSerial(boss->BossTurn().nextMinionSerial++);
        spawned.push_back(minion);
    }
    return spawned;
}

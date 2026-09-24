#pragma once

#include <string>
#include <vector>
#include "utility.h"
#include "BallStatus.h"
#include "StatusEffect.h"
#include "MeshRenderer.h"

// 敵が生成するお邪魔ボールの設定を保持する。
struct NuisanceBallData
{
    bool enabled = false;                 // trueの場合、お邪魔ボール生成ギミックを使用する
    int initialDelayTurns = 2;            // 最初のお邪魔ボール生成までの待機ターン数
    int respawnDelayTurns = 3;            // 再生成までの待機ターン数
    DirectX::SimpleMath::Vector3 spawnOffset =
        DirectX::SimpleMath::Vector3(12.0f, 0.0f, 0.0f);   // 敵位置を基準とした生成位置のオフセット
    float radius = 2.0f;                  // お邪魔ボールの半径
    float mass = 1.0f;                    // お邪魔ボールの質量
    float restitution = 0.8f;             // 衝突時の反発係数
    float friction = 0.025f;              // 移動時に使用する摩擦値
    StatusEffectCollection debuffs;       // お邪魔ボールによって付与するデバフ
};

// 敵1種類分の基本性能、表示モデル、報酬、固有ギミック設定を保持する。
struct EnemyData
{
    std::string id = "enemy_default";      // 敵定義を識別するID

    std::string modelFilePath = "assets/model/GolfBall/golf_ball.obj";  // 描画に使用するモデルファイル
    std::string textureDirectory = "assets/model/GolfBall";             // モデル用テクスチャの検索先

    int maxHp = 3;                                  // 最大HP
    BallStatus status;                              // 質量・半径・防御などボールとしての基本性能
    StatusEffectCollection initialStatusEffects;    // 敵マスタではなく、ステージ上の各配置が個別に設定する初期状態効果
    float frontalDamageMultiplier = 1.0f;           // 正面から受けるダメージへ掛ける倍率
    float pocketDamageRatio = 0.0f;                 // ポケット時に使用するダメージ比率

    // 空の場合、この敵は衝突回数による段階式の弱点ギミックを持たない。
    // 最終段階へ到達した後は、配列末尾の倍率を継続して使用する。
    std::vector<float> collisionDamageMultipliers;
    int collisionCountGraceTicks = 6;       // 同一衝突を重複カウントしないための猶予tick数
    NuisanceBallData nuisanceBall;          // お邪魔ボール生成ギミックの設定

    DirectX::SimpleMath::Vector3 initPosition =
        DirectX::SimpleMath::Vector3(50.0f, 0.0f, 50.0f);   // 敵定義側で保持する初期位置

    DirectX::SimpleMath::Vector3 scale =
        DirectX::SimpleMath::Vector3(2.4f, 2.4f, 2.4f);     // 描画時のモデル拡縮率

    int rewardMoney = 0;                   // 撃破時に獲得するMoney
    int rewardExp = 0;                     // 撃破時に獲得する経験値
};

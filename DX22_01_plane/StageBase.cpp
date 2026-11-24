#include "StageBase.h"
#include "GameBattleCommands.h"
#include "GameBattleView.h"
#include "GameRuntime.h"
#include "GameWorld.h"
#include "GameObject.h"
#include "Input.h"
#include "PlayerBall.h"
//#include "Arrow.h"
#include "Texture2D.h"

/// <summary>
/// 基底ステージを既定の状態で初期化する。
/// </summary>
StageBase::StageBase() {}
/// <summary>
/// ステージの終了処理を呼び出してシーンのオブジェクトを破棄する。
/// </summary>
StageBase::~StageBase() { Uninit(); }

/// <summary>
/// プレイヤータグの付いたオブジェクトからプレイヤーボールを取得する。
/// </summary>
PlayerBall* StageBase::GetPlayerBall() const {
    const auto players =
        GameRuntime::World().GetObjectsWithTag(GameObjectTag::Player);
    for (GameObject* playerObject : players)
    {
        if (PlayerBall* player = playerObject->GetComponent<PlayerBall>())
        {
            return player;
        }
    }

    return nullptr;
}

/// <summary>
/// ターン終了時に打数を更新し、移動中にゴールが成立した場合はステージを完了する。
/// </summary>
void StageBase::Update()
{
    RemoveInvalidSceneObjectRefs();

    PlayerBall* ball = GetPlayerBall();
    if (!ball) return;
   
    switch (GameBattleView::State())
    {
    case BattleState::TurnEnd:
        // TC-20: ショット完了（全ボール停止）のタイミングで打数カウント
        m_StrokeCount++;
        UpdateStrokeUI();
        // 次の戦闘更新で AimingDirection へ遷移する
        break;

    case BattleState::BallsMoving:
        // TC-21: ゴール判定
        if (ball->GetState() == PlayerBall::State::Goal)
        {
            GameBattleCommands::CompleteCurrentStage();
        }
        break;

    default:
		// TC-22: AimingDirection / AimingPower では
        //        何もしない（PlayerBall::UpdateAim() が入力を処理）
        break;
    }
}

/// <summary>
/// 現在の打数を2桁に分け、数字テクスチャの表示を更新する。
/// </summary>
void StageBase::UpdateStrokeUI()
{
    RemoveInvalidSceneObjectRefs();

    std::vector<Texture2D*> textures;

    for (GameObject* gameObject : m_SceneGameObjects)
    {
        if (Texture2D* tex = gameObject->GetComponent<Texture2D>())
        {
            textures.push_back(tex);
        }
    }

    if (textures.size() < 2) return;

    Texture2D* count[2] = {
        textures[textures.size() - 2],
        textures[textures.size() - 1]
    };

    for (int i = 0; i < 2; i++)
    {
        int cnt = m_StrokeCount % (int)pow(10, i + 1) / (int)pow(10, i);
        count[i]->SetUV((float)(cnt + 1), 1, 10, 1);
    }
}

/// <summary>
/// シーンが管理するオブジェクトを破棄し、参照一覧を消去する。
/// </summary>
void StageBase::Uninit() {
    for (GameObject* gameObject : m_SceneGameObjects)
    {
        GameRuntime::DestroyObject(gameObject);
    }
    m_SceneGameObjects.clear();
}

/// <summary>
/// ゲームワールドに存在しないオブジェクトへの参照を一覧から除く。
/// </summary>
void StageBase::RemoveInvalidSceneObjectRefs()
{
    GameWorld& world = GameRuntime::World();
    std::erase_if(m_SceneGameObjects, [&world](GameObject* gameObject) {
        return !world.Contains(gameObject);
        });
}

#include "StageBase.h"
#include "Game.h"
#include "Input.h"
#include "PlayerBall.h"
//#include "Arrow.h"
#include "Texture2D.h"

StageBase::StageBase() {}
StageBase::~StageBase() { Uninit(); }

PlayerBall* StageBase::GetPlayerBall() const {
    const auto players = Game::GetInstance()->GetGameObjectsWithTag(GameObjectTag::Player);
    for (GameObject* playerObject : players)
    {
        if (PlayerBall* player = playerObject->GetComponent<PlayerBall>())
        {
            return player;
        }
    }

    return nullptr;
}

/*
Arrow* StageBase::GetArrow() const {
    return dynamic_cast<Arrow*>(m_MySceneObjects[2]);
}
*/

void StageBase::Update()
{
    RemoveInvalidSceneObjectRefs();

    PlayerBall* ball = GetPlayerBall();
    if (!ball) return;
   
    switch (Game::GetInstance()->GetGameState())
    {
    case GameState::TurnEnd:
        // TC-20: ショット完了（全ボール停止）のタイミングで打数カウント
        m_StrokeCount++;
        UpdateStrokeUI();
        // ※ 翌フレームに Game::Update() が自動で AimingDirection へ遷移させる
        break;

    case GameState::BallsMoving:
        // TC-21: ゴール判定
        if (ball->GetState() == PlayerBall::State::Goal)
        {
            Game::GetInstance()->ChangeScene(RESULT);
        }
        break;

    default:
        // TC-22: AimingDirection / AimingPower / ConfirmShot では
        //        何もしない（PlayerBall::UpdateAim() が入力・状態遷移を処理）
        break;
    }
}

void StageBase::UpdateStrokeUI()
{
    RemoveInvalidSceneObjectRefs();

    std::vector<Texture2D*> textures;

    for (Component* component : m_MySceneObjects)
    {
        if (Texture2D* tex = dynamic_cast<Texture2D*>(component))
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

void StageBase::Uninit() {
    for (Component* component : m_MySceneObjects) { Game::GetInstance()->DeleteComponent(component); }
    m_MySceneObjects.clear();
}

void StageBase::RemoveInvalidSceneObjectRefs()
{
    Game* game = Game::GetInstance();

    std::erase_if(m_MySceneObjects, [game](Component* component) {
        return !game->ContainsComponent(component);
        });
}

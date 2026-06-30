#include "StageBase.h"
#include "Game.h"
#include "Input.h"
#include "PlayerBall.h"
//#include "Arrow.h"
#include "Texture2D.h"

StageBase::StageBase() {}
StageBase::~StageBase() { Uninit(); }

PlayerBall* StageBase::GetPlayerBall() const {
    return dynamic_cast<PlayerBall*>(m_MySceneObjects[0]);
}

/*
Arrow* StageBase::GetArrow() const {
    return dynamic_cast<Arrow*>(m_MySceneObjects[2]);
}
*/

void StageBase::Update()
{
    PlayerBall* ball = GetPlayerBall();
    if (!ball) return;
    //Arrow* arrow = GetArrow();
    //if (!ball || !arrow) return;

    
    // ★どのステージでも共通のショット・静止・遷移ロジックをここに完全集約！
    /*
    switch (m_State)
    {
    case 0: // ボール移動中
        if (ball->GetState() == PlayerBall::State::Idle)
        {
            m_State = 1;
            arrow->SetState(m_State);
            m_StrokeCount++;
            UpdateStrokeUI();
        }
        else if (ball->GetState() == PlayerBall::State::Goal)
        {
            Game::GetInstance()->ChangeScene(RESULT);
        }
        break;

    case 1: // パワー選択へ
        if (Input::GetKeyTrigger(VK_SPACE))
        {
            m_State = 2;
            arrow->SetState(m_State);
        }
        break;

    case 2: // ショットへ
        if (Input::GetKeyTrigger(VK_SPACE))
        {
            m_State = 3;
            arrow->SetState(m_State);
        }
        break;

    case 3: // ショット実行
        if (Input::GetKeyTrigger(VK_SPACE))
        {
            m_State = 0;
            ball->SetState(PlayerBall::State::Simulation);
            arrow->SetState(m_State);
            ball->Shot(arrow->GetVector());
        }
        break;
    }
    */

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
    if (m_MySceneObjects.size() < 10) return;
    Texture2D* count[2] = {
        dynamic_cast<Texture2D*>(m_MySceneObjects[8]),
        dynamic_cast<Texture2D*>(m_MySceneObjects[9])
    };
    if (!count[0] || !count[1]) return;

    for (int i = 0; i < 2; i++) {
        int cnt = m_StrokeCount % (int)pow(10, i + 1) / (int)pow(10, i);
        count[i]->SetUV((float)(cnt + 1), 1, 10, 1);
    }
}

void StageBase::Uninit() {
    for (auto& o : m_MySceneObjects) { Game::GetInstance()->DeleteObject(o); }
    m_MySceneObjects.clear();
}
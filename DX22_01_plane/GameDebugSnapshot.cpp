#include "Game.h"
#include "GameSceneDebugName.h"
#include "GameRuntime.h"
#include "GameWorld.h"
#include "PlayerDeck.h"
#include "PlayerRunStatus.h"
#include "RunController.h"
#include "BallComponent.h"
#include "PlayerBall.h"
#include "EnemyBall.h"

#include <fstream>
#include <iomanip>

namespace
{
    void WriteVector3(
        std::ofstream& file,
        const char* label,
        const DirectX::SimpleMath::Vector3& value)
    {
        file << label << " = ("
            << value.x << ", " << value.y << ", " << value.z << ")\n";
    }

    void WriteBallDebugStatus(
        std::ofstream& file,
        const char* typeName,
        int index,
        BallComponent* ball)
    {
        if (ball == nullptr) return;
        file << "[" << typeName << " " << index << "]\n";
        WriteVector3(file, "Position", ball->GetPosition());
        WriteVector3(file, "Velocity", ball->GetVelocity());
        file << "HP = " << ball->GetHP() << " / " << ball->GetMaxHP() << "\n";
        file << "Radius = " << ball->GetRadius() << "\n";
        file << "IsStopped = " << (ball->IsStopped() ? "true" : "false") << "\n";
        file << "IsDefeated = " << (ball->IsDefeated() ? "true" : "false") << "\n\n";
    }
}
void Game::SaveDebugSnapshot()
{
	std::ofstream file("debug_state_snapshot.txt");

	if (!file)
	{
		return;
	}

	file << std::fixed << std::setprecision(3);

	file << "[Scene]\n";
	file << "Scene = " << GetSceneDebugName(GetCurrentScene()) << "\n";
	file << "[Scene]\n";
	file << "Scene = " << GetSceneDebugName(GetCurrentScene()) << "\n";

	file << "BattleState = "
		<< ToString(GetBattleState())
		<< " (" << static_cast<int>(GetBattleState()) << ")\n";

	file << "ClearReward = "
		<< (IsClearRewardActive() ? "true" : "false")
		<< "\n";

	file << "LastBattleResult = "
		<< ToString(m_LastBattleResult)
		<< "\n";

	file << "AreAllBallsStopped = "
		<< (AreAllBallsStopped() ? "true" : "false") << "\n";

	file << "AreAllEnemiesDefeated = "
		<< (AreAllEnemiesDefeated() ? "true" : "false") << "\n";
	file << "AreAllBallsStopped = " << (AreAllBallsStopped() ? "true" : "false") << "\n";
	file << "AreAllEnemiesDefeated = " << (AreAllEnemiesDefeated() ? "true" : "false") << "\n";
	file << "\n";

	std::vector<PlayerBall*> players =
		GameRuntime::World().GetComponents<PlayerBall>();
	std::vector<EnemyBall*> enemies =
		GameRuntime::World().GetComponents<EnemyBall>();

	file << "[BallCounts]\n";
	file << "PlayerRunCurrentHp = "
		<< m_RunController->Status().currentHp << "\n";

	file << "PlayerRunMaxHp = "
		<< m_RunController->Status().maxHp << "\n";

	file << "[Deck]\n";
	file << "DrawPile = "
		<< m_RunController->Deck().GetDrawPileCount() << "\n";

	file << "DiscardPile = "
		<< m_RunController->Deck().GetDiscardPileCount() << "\n";

	file << "OfferCount = "
		<< m_RunController->Deck().GetOfferCount() << "\n";

	file << "TotalDeckCount = "
		<< m_RunController->Deck().GetRewardTargetCount() << "\n";

	const PlayerBallData* currentBall = m_RunController->Deck().GetCurrent();
	if (currentBall != nullptr)
	{
		file << "CurrentBallId = "
			<< currentBall->definitionId
			<< "\n";

		file << "CurrentBallAttack = "
			<< currentBall->status.attack
			<< "\n";

		file << "CurrentBallUsed = "
			<< (m_RunController->Deck().IsCurrentUsed() ? "true" : "false")
			<< "\n";
	}
	else
	{
		file << "CurrentBallId = none\n";
	}

	file << "\n";
	file << "PlayerBall = " << players.size() << "\n";
	file << "EnemyBall = " << enemies.size() << "\n";
	file << "\n";

	file << "PlayerMoney = "
		<< m_RunController->Status().money
		<< "\n";

	for (int i = 0; i < static_cast<int>(players.size()); i++)
	{
		WriteBallDebugStatus(file, "PlayerBall", i, players[i]->GetBall());
	}

	for (int i = 0; i < static_cast<int>(enemies.size()); i++)
	{
		WriteBallDebugStatus(file, "EnemyBall", i, enemies[i]->GetBall());
	}
}


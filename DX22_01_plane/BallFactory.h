#pragma once

#include "SimpleMath.h"

class EnemyBall;
class BreakBall;
class Game;
class PlayerBall;
class NuisanceBall;
class PierceMinion;
struct BallStatus;
struct EnemyData;
struct NuisanceBallData;

// Creates each ball GameObject with the shared status, physics, collider,
// rendering, collision, and BallComponent composition.
class BallFactory final
{
public:
    // Create the player ball.
    static PlayerBall* CreatePlayer(Game& game);

    // Create a break ball for the specified index.
    static BreakBall* CreateBreakBall(Game& game, int index);

    // Create an enemy ball and its attack component.
    static EnemyBall* CreateEnemy(Game& game, const EnemyData& data);

    // Create a nuisance ball at the supplied position.
    static NuisanceBall* CreateNuisanceBall(
        const NuisanceBallData& data,
        const DirectX::SimpleMath::Vector3& position);

	static PierceMinion* CreatePierceMinion(
		Game& game,
		const BallStatus& status,
		const DirectX::SimpleMath::Vector3& position,
		const DirectX::SimpleMath::Vector3& direction,
		float launchSpeed);
};

#pragma once

#include "BallTrajectoryPreview.h"

#include <cstdint>

class PlayerBall;
struct PlayerBallData;

namespace DirectX::SimpleMath
{
    struct Vector3;
}

namespace BallShotPrediction
{
    struct Result;
}

// Runs the shared shot predictor against the active game context without
// exposing the Game orchestrator to the player input component.
class GameShotPrediction final
{
public:
	static BallTrajectoryPreview::Result Preview(
		const PlayerBall& player,
		const DirectX::SimpleMath::Vector3& velocity,
		bool skipFirstPlayerFriction);
	static std::uint64_t PreviewWorldKey();
    static BallShotPrediction::Result Predict(
        const PlayerBall& player,
        const DirectX::SimpleMath::Vector3& velocity,
        bool skipFirstPlayerFriction,
        const PlayerBallData* offer = nullptr);
    static std::uint64_t WorldKey();
    static void WriteVerification(
        const PlayerBall& player,
        const DirectX::SimpleMath::Vector3& velocity,
        bool skipFirstPlayerFriction);
    static void WriteVerificationActual();

    GameShotPrediction() = delete;
};

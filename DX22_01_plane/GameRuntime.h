#pragma once

#include <cstddef>
#include <string>

class Camera;
class Game;
class GameObject;
class GameWorld;
class SceneObjectFactory;

// Stable entry points for the application shell and object infrastructure.
// Gameplay rules remain owned by Game and its dedicated controllers.
class GameRuntime final
{
public:
    static void Initialize();
    static void Update(double elapsedSeconds);
    static void ResetFrameTiming();
    static void RecordFrameTiming(
        double frameMilliseconds,
        double updateMilliseconds,
        double drawMilliseconds,
        double presentMilliseconds);
    static bool IsFrameTimingMeasurementActive();
    static void RecordShotPredictionTiming(double milliseconds);
	static void RecordTrajectoryPreviewPrediction(
		double milliseconds,
		std::size_t copiedBalls);
	static void RecordTrajectoryPreviewWorldKey(double milliseconds);
	static void RecordTrajectoryPreviewCacheReuse();
    static void Draw();
    static void Shutdown();

    static Camera& MainCamera();
    static GameWorld& World();

    static GameObject* CreateObject(
        Game& game,
        const std::string& name);
    static GameObject* CreateObject(const std::string& name);
    static void DestroyObject(GameObject* object);

    GameRuntime() = delete;

private:
    friend class SceneObjectFactory;
    static Game& CurrentGame();
};

#pragma once

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
    static void Draw();
    static void Shutdown();

    static Camera& MainCamera();
    static GameWorld& World();

    static GameObject* CreateObject(
        Game& game,
        const std::string& name);
    static void DestroyObject(GameObject* object);

    GameRuntime() = delete;

private:
    friend class SceneObjectFactory;
    static Game& CurrentGame();
};

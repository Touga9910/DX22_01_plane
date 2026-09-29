#pragma once

class Texture2D;

// Creates objects owned by the current scene without exposing Game to UI code.
class SceneObjectFactory final
{
public:
    static Texture2D* CreateTexture2D();

    SceneObjectFactory() = delete;
};

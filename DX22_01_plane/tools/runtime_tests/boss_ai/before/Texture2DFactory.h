#pragma once

class Game;
class Texture2D;

class Texture2DFactory final
{
public:
    static Texture2D* Create(Game& game);
};

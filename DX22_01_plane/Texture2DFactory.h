#pragma once

class Game;
class Texture2D;

// 画面UI用Texture2Dを持つGameObjectを生成するFactory
class Texture2DFactory final
{
public:
    // ScreenUiタグを持つGameObjectを生成し、追加したTexture2Dを返す
    static Texture2D* Create(Game& game);
};

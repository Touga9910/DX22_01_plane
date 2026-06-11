#pragma once
#include "StageBase.h"

class Stage1Scene : public StageBase // ★StageBaseを継承
{
public:
    Stage1Scene();
    ~Stage1Scene() override = default;

    void Init();
    // もしステージ固有の追加ギミックを動かしたい場合はここでオーバーライドして使用する
    void Update() override;
};
#pragma once
#include "StageBase.h"

class Stage2Scene : public StageBase
{
public:
    Stage2Scene();
    ~Stage2Scene() override = default;

    void Init();
    // もしステージ固有の追加ギミックを動かしたい場合はここでオーバーライドして使用する
    void Update() override;
};
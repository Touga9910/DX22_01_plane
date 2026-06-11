#pragma once
#include "StageBase.h"

class Stage3Scene : public StageBase
{
public:
    Stage3Scene();
    ~Stage3Scene() override = default;

    void Init();
    // もしステージ固有の追加ギミックを動かしたい場合はここでオーバーライドして使用する
    void Update() override;
};
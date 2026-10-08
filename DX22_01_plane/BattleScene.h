#pragma once
#include "StageBase.h"

#include <memory>

class BattleDebugHotReload;
class BattleSceneObjectFactory;
class BattleSceneServices;
class BattleSceneSetup;

class BattleScene : public StageBase
{
public:
    explicit BattleScene(
        std::unique_ptr<BattleSceneServices> services);
    ~BattleScene() override;

    void Init();
    void Update() override;

private:
    std::unique_ptr<BattleSceneServices> m_Services;
    std::unique_ptr<BattleSceneSetup> m_Setup;
    std::unique_ptr<BattleSceneObjectFactory> m_ObjectFactory;
    std::unique_ptr<BattleDebugHotReload> m_HotReload;
};

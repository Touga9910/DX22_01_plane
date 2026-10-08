#include "BattleScene.h"

#include "BattleDebugHotReload.h"
#include "BattleSceneObjectFactory.h"
#include "BattleSceneServices.h"
#include "BattleSceneSetup.h"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>

namespace
{
    BattleSceneServices& RequireServices(
        const std::unique_ptr<BattleSceneServices>& services)
    {
        if (services == nullptr)
        {
            throw std::invalid_argument(
                "BattleScene requires BattleSceneServices");
        }
        return *services;
    }
}

BattleScene::BattleScene(
    std::unique_ptr<BattleSceneServices> services)
    : m_Services(std::move(services))
    , m_Setup(std::make_unique<BattleSceneSetup>(
        RequireServices(m_Services)))
    , m_ObjectFactory(std::make_unique<BattleSceneObjectFactory>(
        RequireServices(m_Services)))
    , m_HotReload(std::make_unique<BattleDebugHotReload>(
        RequireServices(m_Services)))
{
    Init();
}

BattleScene::~BattleScene() = default;

void BattleScene::Init()
{
    m_Par = 4;
    m_StrokeCount = 0;

    std::cout << "\u30aa\u30d6\u30b8\u30a7\u30af\u30c8\u3092\u751f\u6210\u958b\u59cb\n" << std::endl;

    const BattleArenaObjects arena =
        m_ObjectFactory->CreateArena(m_SceneGameObjects);
    BattleSceneSetupResult setup = m_Setup->Prepare();
    m_HotReload->Initialize(
        std::move(setup.stageJsonPath),
        std::move(setup.enemyJsonPath),
        setup.selectedStageId);

    if (setup.stage.has_value())
    {
        m_Par = setup.stage->par;
        m_Setup->ValidateEnemySpawns(
            *setup.stage,
            *arena.player,
            *arena.tableFrame);
        m_ObjectFactory->CreateStageObjects(
            *setup.stage,
            m_SceneGameObjects);
    }

    std::cout << "\n\u30aa\u30d6\u30b8\u30a7\u30af\u30c8\u306e\u751f\u6210\u7d42\u4e86\n" << std::endl;
    m_ObjectFactory->BeginSimulation(*arena.player);
}

void BattleScene::Update()
{
    StageBase::Update();
    m_HotReload->Update();
}

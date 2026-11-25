#include "Game.h"
#include "PlayerDeck.h"
#include "PlayerRunStatus.h"
#include "RunController.h"
#include "RunEventRuntime.h"
#include "RunProgressController.h"
#include "json/json.hpp"

#include <utility>

namespace
{
const RunMapNode* ActiveEvent(const RunMap& map)
{
    const auto* node = map.Node(map.Active());
    return node && node->type == StageRouteType::Event ? node : nullptr;
}
}

void Game::PrepareRunEvent()
{
    if (!m_RunEventId.empty()) return;
    const auto& map = GetRunMap();
    const auto* node = ActiveEvent(map);
    if (!node) return;
    m_RunEventId = IsDebugRunMode() && FindRunEventDefinition(m_DebugRunEventId)
        ? m_DebugRunEventId
        : RunEvents::SelectEventId(map, *node, m_RunController->Status(), m_RunController->Deck());
    RecordBalanceEvent("run_event_enter", {
        { "event", m_RunEventId }, { "node_id", node->id },
        { "hp", m_RunController->Status().currentHp }, { "money", m_RunController->Status().money },
    });
}

RunEventViewSnapshot Game::CaptureRunEvent() const
{
    if (GetCurrentSceneType() != SceneType::Event || GetRunPhase() != RunPhase::NormalRoute) return {};
    const auto& map = GetRunMap();
    const auto* node = ActiveEvent(map);
    if (!node) return {};
    const auto& status = m_RunController->Status();
    auto offer = RunEvents::Build(map, *node, status, m_RunController->Deck(), m_RunEventActions, m_RunEventId);
    RunEventViewSnapshot snapshot{ offer.title, status.currentHp, status.maxHp, status.money, {}, offer.id };
    snapshot.result = m_RunEventResult;
    snapshot.deck = RunEvents::CaptureDeck(m_RunController->Deck());
    if (!snapshot.result.pending)
        for (auto& option : offer.options) snapshot.choices.push_back(std::move(option.choice));
    return snapshot;
}

bool Game::ChooseRunEvent(int choiceIndex, int targetIndex)
{
    if (GetCurrentSceneType() != SceneType::Event || GetRunPhase() != RunPhase::NormalRoute) return false;
    const auto& map = GetRunMap();
    const auto* node = ActiveEvent(map);
    if (!node) return false;
    RunEvents::Application application;
    if (!RunEvents::Apply(map, *node, m_RunEventId, *m_RunController, m_RunEventActions,
        m_RunEventResult, choiceIndex, targetIndex, application)) return false;

    if (application.effectId == "upgrade" || application.effectId == "remove" || application.effectId == "trade")
        RemoveBalanceAutoPendingBall(application.targetInstanceId);
    if (!application.acquiredBallId.empty()) NotifyBallAcquired(application.acquiredBallId);
    RecordBalanceEvent("run_event_choice", {
        { "event", m_RunEventId }, { "node_id", node->id }, { "choice_index", choiceIndex },
        { "effect", application.effectId }, { "target_instance_id", application.targetInstanceId },
        { "acquired_ball_id", application.acquiredBallId },
        { "hp_before", m_RunEventResult.hpBefore }, { "hp_after", m_RunEventResult.hpAfter },
        { "money_before", m_RunEventResult.moneyBefore }, { "money_after", m_RunEventResult.moneyAfter },
        { "money_award", application.moneyAward }, { "polish_actions", m_RunEventActions },
    });
    SaveRunCheckpoint(SceneType::Event, false, false);
    return true;
}

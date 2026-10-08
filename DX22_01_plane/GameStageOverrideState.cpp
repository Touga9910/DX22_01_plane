#include "GameStageOverrideState.h"

#include "StageData.h"

#include <optional>
#include <utility>

struct GameStageOverrideState::Impl
{
	std::optional<StageData> queued;
	std::optional<StageData> current;
};

GameStageOverrideState::GameStageOverrideState()
	: m_Impl(std::make_unique<Impl>())
{
}

GameStageOverrideState::~GameStageOverrideState() = default;

void GameStageOverrideState::Queue(StageData stage)
{
	m_Impl->queued = std::move(stage);
}

void GameStageOverrideState::ClearQueued()
{
	m_Impl->queued.reset();
}

const StageData* GameStageOverrideState::Queued() const
{
	return m_Impl->queued.has_value()
		? &m_Impl->queued.value()
		: nullptr;
}

StageData* GameStageOverrideState::PromoteQueuedToCurrent()
{
	if (!m_Impl->queued.has_value())
	{
		return nullptr;
	}
	m_Impl->current = std::move(m_Impl->queued.value());
	m_Impl->queued.reset();
	return &m_Impl->current.value();
}

void GameStageOverrideState::ClearCurrent()
{
	m_Impl->current.reset();
}

bool GameStageOverrideState::HasCurrent() const
{
	return m_Impl->current.has_value();
}

const StageData* GameStageOverrideState::Current() const
{
	return m_Impl->current.has_value()
		? &m_Impl->current.value()
		: nullptr;
}

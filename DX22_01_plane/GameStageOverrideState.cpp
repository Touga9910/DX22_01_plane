#include "GameStageOverrideState.h"

#include "StageData.h"

#include <optional>
#include <utility>

struct GameStageOverrideState::Impl
{
	std::optional<StageData> queued;
	std::optional<StageData> current;
};

/// <summary>
/// 試遊などに使うステージ上書き情報の内部状態を生成する。
/// </summary>
GameStageOverrideState::GameStageOverrideState()
	: m_Impl(std::make_unique<Impl>())
{
}

/// <summary>
/// ステージ上書き用の内部状態を解放する。
/// </summary>
GameStageOverrideState::~GameStageOverrideState() = default;

/// <summary>
/// 渡されたステージを次回適用する上書き候補として保存する。
/// </summary>
void GameStageOverrideState::Queue(StageData stage)
{
	m_Impl->queued = std::move(stage);
}

/// <summary>
/// 次回適用する上書き候補を取り消す。
/// </summary>
void GameStageOverrideState::ClearQueued()
{
	m_Impl->queued.reset();
}

/// <summary>
/// 次回適用する上書き候補があれば返す。
/// </summary>
const StageData* GameStageOverrideState::Queued() const
{
	return m_Impl->queued.has_value()
		? &m_Impl->queued.value()
		: nullptr;
}

/// <summary>
/// 上書き候補を適用中のステージへ移し、候補を取り除いて適用先を返す。
/// </summary>
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

/// <summary>
/// 適用中のステージ上書きを解除する。
/// </summary>
void GameStageOverrideState::ClearCurrent()
{
	m_Impl->current.reset();
}

/// <summary>
/// 現在適用されているステージ上書きがあるかを返す。
/// </summary>
bool GameStageOverrideState::HasCurrent() const
{
	return m_Impl->current.has_value();
}

/// <summary>
/// 現在適用されているステージ上書きがあれば返す。
/// </summary>
const StageData* GameStageOverrideState::Current() const
{
	return m_Impl->current.has_value()
		? &m_Impl->current.value()
		: nullptr;
}

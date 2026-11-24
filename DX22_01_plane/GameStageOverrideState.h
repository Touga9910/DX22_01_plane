#pragma once

#include <memory>

struct StageData;

// Owns externally supplied stage layouts independently from Game's orchestration
// surface. MCP and debug setup share the same queue/current lifecycle.
class GameStageOverrideState final
{
private:
	struct Impl;
	std::unique_ptr<Impl> m_Impl;

public:
	GameStageOverrideState();
	~GameStageOverrideState();

	/// <summary>
	/// 内部状態の所有権が重複するコピー構築を禁止する。
	/// </summary>
	GameStageOverrideState(const GameStageOverrideState&) = delete;
	/// <summary>
	/// 内部状態の所有権が重複するコピー代入を禁止する。
	/// </summary>
	GameStageOverrideState& operator=(const GameStageOverrideState&) = delete;

	void Queue(StageData stage);
	void ClearQueued();
	const StageData* Queued() const;

	StageData* PromoteQueuedToCurrent();
	void ClearCurrent();
	bool HasCurrent() const;
	const StageData* Current() const;
};

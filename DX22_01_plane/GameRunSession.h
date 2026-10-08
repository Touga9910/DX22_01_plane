#pragma once

#include "RunResultSnapshot.h"

// Owns the lifetime state of one run and its immutable completed result.
// Game decides when transitions happen; this class keeps the related state
// changes consistent and keeps RunResultSnapshot out of Game.h.
class GameRunSession final
{
public:
	bool IsActive() const;
	void Begin();
	void Activate();
	void Deactivate();
	void Complete(RunResultSnapshot result);
	const RunResultSnapshot& LastResult() const;

private:
	bool m_Active = false;
	RunResultSnapshot m_LastResult{};
};

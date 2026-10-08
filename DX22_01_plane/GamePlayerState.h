#pragma once

class PlayerBall;

// Synchronizes the active player ball with run-owned status and defenses.
// Gameplay objects do not need the Game orchestrator to perform this exchange.
class GamePlayerState final
{
public:
    static void ApplyTo(PlayerBall* player);
    static void CaptureFrom(const PlayerBall* player);
    static int PocketDamageAmount();
    static int AbsorbShieldDamage(int damage);

    GamePlayerState() = delete;
};

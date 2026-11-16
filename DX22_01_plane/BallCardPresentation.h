#pragma once

struct PlayerBallData;
struct ImVec2;

namespace BallCardPresentation
{
    struct BallCardInteraction
    {
        bool select = false;
        bool openDetails = false;
    };

    void LoadResources();
    void ResetResources();
    void DrawBallIcon(const PlayerBallData& ball, const ImVec2& position, const ImVec2& size);
    BallCardInteraction DrawBallSelectionCard(
        const PlayerBallData& ball, int index, int attack,
        const ImVec2& position, const ImVec2& size, bool selected, bool held);
    void DrawBallDetailPopup(const PlayerBallData& ball, int effectiveAttack);
}

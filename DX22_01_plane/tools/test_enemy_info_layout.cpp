#include "EnemyInfoLayout.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
    /// <summary>
    /// 配置の検査に失敗したとき、説明を表示してテストを終了する。
    /// </summary>
    void Check(bool value, const char* message)
    {
        if (!value) { std::cerr << message << '\n'; std::exit(1); }
    }

    /// <summary>
    /// 二つの座標のx成分とy成分が一致するかを返す。
    /// </summary>
    bool Equal(const ImVec2& a, const ImVec2& b)
    {
        return a.x == b.x && a.y == b.y;
    }
}

/// <summary>
/// 敵の情報表示が盤面上の予約領域や球を避けて配置され、再現性を保つことを検証する。
/// </summary>
int main()
{
    const EnemyInfoLayout::BodyRegions empty;
    EnemyInfoLayout layout({0, 0}, {800, 600}, 40, empty);
    const ImVec2 base(300, 200);
    const ImVec2 ball(310, 140);
    const auto first = layout.Resolve(base, 20, 20, true, 1, ball, 14);
    Check(!first.hidden && !first.moved && Equal(first.position, base),
        "An unobstructed panel must stay at its base position");

    const auto second = layout.Resolve(base, 20, 20, true, 2, ball, 14);
    Check(!second.hidden && second.moved && second.position.y < base.y,
        "A reserved panel must move the next panel upward first");
    Check(second.position.y + 20 + 4 + 2 <= first.position.y - 4,
        "Panel frames must keep the original padding and gap");

    EnemyInfoLayout nextFrame({0, 0}, {800, 600}, 40, empty);
    Check(Equal(nextFrame.Resolve(base, 20, 20, true, 1, ball, 14).position, base),
        "Placement reservations must not leak into the next frame");

    const EnemyInfoLayout::BodyRegions bodies{{1, {290, 190, 330, 230}}};
    EnemyInfoLayout ownBody({0, 0}, {800, 600}, 40, bodies);
    Check(!ownBody.Resolve(base, 20, 20, true, 1, ball, 14).hidden,
        "An enemy's own body is excluded from obstruction tests");
    EnemyInfoLayout otherBody({0, 0}, {800, 600}, 40, bodies);
    const auto obstructed = otherBody.Resolve(base, 20, 20, true, 2, ball, 14);
    Check(obstructed.hidden || obstructed.moved,
        "Another enemy's body must prevent use of the overlapping base position");

    EnemyInfoLayout offsetViewport({100, 50}, {640, 480}, 120, empty);
    Check(Equal(offsetViewport.ClampHoverPosition({-1000, -1000}, 20, 30), {106, 124}),
        "Hover panels must respect viewport origin and the safe top edge");
    Check(Equal(offsetViewport.ClampHoverPosition({10000, 10000}, 20, 30), {714, 494}),
        "Hover panels must respect the right and bottom edges");

    EnemyInfoLayout smallViewport({0, 0}, {80, 80}, 20, empty);
    Check(smallViewport.Resolve({30, 30}, 1000, 1000, false, 1, {40, 40}, 14).hidden,
        "When no candidate fits, preserve the hidden-info fallback");

    EnemyInfoLayout replayA({0, 0}, {800, 600}, 40, bodies);
    EnemyInfoLayout replayB({0, 0}, {800, 600}, 40, bodies);
    for (int index = 0; index < 20; ++index)
    {
        const ImVec2 position(280.0f + index * 4.0f, 180.0f + (index % 3) * 12.0f);
        const auto a = replayA.Resolve(position, 24, 20, index % 2 == 0, index + 1, ball, 14);
        const auto b = replayB.Resolve(position, 24, 20, index % 2 == 0, index + 1, ball, 14);
        Check(a.hidden == b.hidden && a.moved == b.moved && Equal(a.position, b.position)
            && Equal(a.link.start, b.link.start) && Equal(a.link.end, b.link.end),
            "Identical inputs and reservation order must produce identical placements");
        Check(std::isfinite(a.position.x) && std::isfinite(a.position.y),
            "Placement must remain finite even in dense arrangements");
    }
    std::cout << "EnemyInfoLayout: placement, reservations, body exclusion, bounds, fallback, determinism passed\n";
}

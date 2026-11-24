#include "../SceneTransition.h"
#include <cassert>
#include <iostream>
#include <limits>
#include <stdexcept>

int main()
{
    for (auto phase : {SceneTransition::Phase::FadeOut, SceneTransition::Phase::Black, SceneTransition::Phase::FadeIn})
    {
        SceneTransition cancelled;
        assert(cancelled.RequestPreview({.2f,.3f,.4f}));
        while (cancelled.GetPhase() != phase) cancelled.Advance(.1);
        assert(cancelled.IsPreviewing() && cancelled.CancelPreview());
        assert(!cancelled.Active() && !cancelled.IsPreviewing() && cancelled.Alpha() == 0);
        assert(!cancelled.CancelPreview() && !cancelled.Advance(.1));
        int sceneChanges = 0;
        // Cancellation preserves the timing and never cancels a real transition.
        assert(cancelled.Request([&] { ++sceneChanges; }));
        assert(!cancelled.CancelPreview());
        cancelled.Advance(.1); assert(std::abs(cancelled.Alpha() - .5f) < .001f);
        while (cancelled.Active()) cancelled.Advance(.1);
        assert(sceneChanges == 1);
        assert(cancelled.RequestPreview({.2f,.3f,.4f}));
        assert(cancelled.CancelPreview());
    }
    SceneTransition custom;
    custom.Configure({.2f, .3f, .4f});
    int customCommits = 0;
    custom.Request([&] { ++customCommits; });
    custom.Advance(.1); assert(std::abs(custom.Alpha() - .5f) < .001f);
    custom.Configure({0,0,0}); // Active transitions retain their starting timing.
    custom.Advance(.1); assert(custom.GetPhase() == SceneTransition::Phase::Black);
    custom.Advance(.1); custom.Advance(.1); assert(customCommits == 0 && custom.Alpha() == 1);
    custom.Advance(.1); assert(customCommits == 1 && custom.Alpha() == 1);
    custom.Advance(.1); assert(custom.Alpha() > .8f && custom.Alpha() < .9f);
    custom.Advance(.1); custom.Advance(.1); custom.Advance(.1); assert(!custom.Active());
    custom.Configure({0,0,0});
    custom.Request([&] { ++customCommits; });
    assert(std::isfinite(custom.Alpha()));
    custom.Advance(0); assert(customCommits == 1 && custom.Alpha() == 1);
    custom.Advance(0); assert(customCommits == 2 && std::isfinite(custom.Alpha()));
    custom.Advance(0); assert(!custom.Active());
    SceneTransition preview;
    assert(preview.RequestPreview({.2f, .3f, .4f}));
    assert(!preview.RequestPreview({0,0,0}));
    preview.Advance(.1); assert(std::abs(preview.Alpha() - .5f) < .001f);
    preview.Advance(.1); assert(preview.GetPhase() == SceneTransition::Phase::Black);
    preview.Advance(.1); preview.Advance(.1); assert(preview.Alpha() == 1);
    preview.Advance(.1); assert(preview.GetPhase() == SceneTransition::Phase::FadeIn);
    preview.Advance(.1); assert(preview.Alpha() > .8f && preview.Alpha() < .9f);
    while (preview.Active()) preview.Advance(.1);
    assert(!preview.IsPreviewing() && !preview.CancelPreview());
    assert(preview.RequestPreview({0,0,0}));
    for (int i = 0; i < 3; ++i) preview.Advance(0);
    assert(!preview.Active() && preview.Alpha() == 0);
    assert(preview.RequestPreview({.2f,.3f,.4f})); // Can replay after completion.
    preview.Reset();
    SceneTransition transition;
    assert(!transition.Active() && transition.Alpha() == 0 && !transition.Advance(.016));
    int commits = 0;
    assert(transition.Request([&] { assert(transition.IsCommitting() && transition.Alpha() == 1); ++commits; }));
    assert(!transition.Request([&] { commits += 100; }));
    float alpha = 0;
    while (transition.GetPhase() == SceneTransition::Phase::FadeOut)
    {
        assert(transition.Advance(1.0 / 60));
        assert(transition.Alpha() >= alpha && transition.Alpha() <= 1 && commits == 0);
        alpha = transition.Alpha();
    }
    // Hold a fully black screen before destroying the old scene or fading in.
    assert(transition.Alpha() == 1 && commits == 0);
    for (int i = 0; i < 4; ++i)
    {
        transition.Advance(.1);
        assert(transition.GetPhase() == SceneTransition::Phase::Black && transition.Alpha() == 1 && commits == 0);
    }
    assert(!transition.Request([&] { commits += 100; }));
    transition.Advance(.09);
    assert(transition.GetPhase() == SceneTransition::Phase::Black && transition.Alpha() == 1 && commits == 0);
    transition.Advance(.02);
    assert(commits == 1 && !transition.IsCommitting() && transition.Alpha() == 1);
    while (transition.Active())
    {
        assert(transition.Advance(.016));
        assert(transition.Alpha() <= alpha && transition.Alpha() >= 0 && commits == 1);
        alpha = transition.Alpha();
    }
    assert(!transition.Advance(.016));
    assert(transition.Request([&] { ++commits; }, true));
    assert(transition.Alpha() == 1 && commits == 1);
    for (int i = 0; i < 4; ++i)
    {
        transition.Advance(.1); assert(commits == 1 && transition.Alpha() == 1);
    }
    transition.Advance(.1); assert(commits == 2 && transition.Alpha() == 1);
    transition.Reset();
    transition.BeginFadeIn(); assert(transition.Alpha() == 1);
    while (transition.Active()) transition.Advance(.016);
    assert(commits == 2);
    assert(transition.Request([&] { ++commits; }));
    transition.Reset(); transition.Advance(100); assert(commits == 2);
    assert(transition.Request([] { throw std::runtime_error("failed load"); }, true));
    bool rejected = false;
    try { for (int i = 0; i < 5; ++i) transition.Advance(.1); } catch (...) { rejected = true; }
    assert(rejected && !transition.IsCommitting());
    while (transition.Active()) transition.Advance(.016);
    assert(transition.Alpha() == 0);
    assert(!transition.Request({}));
    assert(transition.Request([] {}));
    transition.Advance(-1); transition.Advance(std::numeric_limits<double>::quiet_NaN());
    assert(transition.Alpha() == 0);
    transition.Advance(99); assert(transition.GetPhase() == SceneTransition::Phase::FadeOut);
    std::cout << "PASS: fade endpoints/monotonicity, 0.5-second black hold before commit/fade-in, duplicate rejection, one-shot commit, title handoff hold, initial fade-in, reset, failed-load recovery, timing clamps\n";
}

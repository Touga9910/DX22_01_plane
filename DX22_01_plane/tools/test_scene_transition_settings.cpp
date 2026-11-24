#include "../SettingsManager.h"
#include "../Application.h"
#include "../json/json.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>

// Persistence test does not open a native display.
/// <summary>
/// 設定の保存と読み込みを検証するため、画面モード変更を空の実装に置き換える。
/// </summary>
void Application::SetDisplayMode(uint32_t, uint32_t, bool) {}

/// <summary>
/// シーン遷移の時間設定を保存と復元で保持し、古い設定や不正値も処理できることを検証する。
/// </summary>
int main()
{
    using Json = nlohmann::json;
    const std::string path = "tools/runtime_tests/scene_transition_settings/settings.json";
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    auto write = [&](const Json& value) { std::ofstream file(path); file << value.dump(2); assert(file.good()); };
    SettingsManager manager;
    write({{"bgm_volume", .31f}, {"camera_shake_enabled", false}});
    assert(manager.Load(path));
    assert(manager.Get().sceneTransition.fadeOutSeconds == .55f && manager.Get().sceneTransition.blackHoldSeconds == .5f && manager.Get().sceneTransition.fadeInSeconds == .75f);
    assert(!manager.Get().cameraShakeEnabled && manager.Get().bgmVolume == .31f);
    manager.Edit().sceneTransition = {.2f, .4f, .8f}; manager.MarkDirty();
    assert(manager.SaveIfDirty(path) && !manager.IsDirty());
    SettingsManager restored; assert(restored.Load(path));
    assert(restored.Get().sceneTransition.fadeOutSeconds == .2f && restored.Get().sceneTransition.blackHoldSeconds == .4f && restored.Get().sceneTransition.fadeInSeconds == .8f);
    assert(!restored.Get().cameraShakeEnabled && restored.Get().bgmVolume == .31f);
    write({{"bgm_volume", .2f}, {"scene_transition", {{"fade_out_seconds", -2}, {"black_hold_seconds", 50}, {"fade_in_seconds", "invalid"}}}});
    assert(manager.Load(path));
    assert(manager.Get().sceneTransition.fadeOutSeconds == 0 && manager.Get().sceneTransition.blackHoldSeconds == 3 && manager.Get().sceneTransition.fadeInSeconds == .75f && manager.Get().bgmVolume == .2f);
    write({{"scene_transition", {{"fade_out_seconds", 1e300}, {"black_hold_seconds", nullptr}, {"fade_in_seconds", 0}}}});
    assert(manager.Load(path));
    assert(manager.Get().sceneTransition.fadeOutSeconds == .55f && manager.Get().sceneTransition.blackHoldSeconds == .5f && manager.Get().sceneTransition.fadeInSeconds == 0);
    write({{"scene_transition", false}}); assert(manager.Load(path));
    assert(manager.Get().sceneTransition.fadeInSeconds == .75f);
    manager.Edit().sceneTransition = {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -1};
    assert(manager.Save(path) && restored.Load(path));
    assert(restored.Get().sceneTransition.fadeOutSeconds == .55f && restored.Get().sceneTransition.blackHoldSeconds == .5f && restored.Get().sceneTransition.fadeInSeconds == 0);
    std::cout << "PASS: independent timing roundtrip, unrelated settings preserved, old-file defaults, malformed/nonfinite timing fallback, range clamps, zero duration\n";
}

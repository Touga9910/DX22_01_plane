#pragma once
#include "StageBase.h"

#include <string>
#include <filesystem>

class Stage1Scene : public StageBase // ★StageBaseを継承
{
public:
    Stage1Scene();
    ~Stage1Scene() override = default;

    void Init();
    // もしステージ固有の追加ギミックを動かしたい場合はここでオーバーライドして使用する
    void Update() override;

private:
    std::string m_StageJsonPath = "assets/data/stage_01.json";
    std::string m_EnemyJsonPath = "assets/data/enemy_data.json";

    std::filesystem::file_time_type m_LastStageJsonWriteTime{};
    std::filesystem::file_time_type m_LastEnemyJsonWriteTime{};

    int m_HotReloadCheckFrame = 0;

    bool m_HotReloadPending = false;
    int m_HotReloadWaitFrame = 0;

	void UpdateJsonHotReload();         // JSONをホットリロードするための関数
	void ReloadEnemyStatusFromJson();   // JSONから敵のステータスをリロードするための関数

    // 更新時刻取得
    std::filesystem::file_time_type GetJsonWriteTime(const std::string& path) const;
};
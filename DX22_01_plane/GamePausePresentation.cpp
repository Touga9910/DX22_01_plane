#include "GamePresentation.h"
#include "GameUi.h"
#include "input.h"
#include "imgui/imgui.h"

#pragma execution_character_set("utf-8")

#include <algorithm>
#include <cmath>

PausePresentationIntent GamePresentation::DrawPause(
	const PausePresentationModel& model)
{
	PausePresentationIntent intent{};
	intent.settings = model.settings;
	GameSettings& settings = intent.settings;
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowViewport(viewport->ID);
	ImGui::SetNextWindowPos(viewport->Pos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(viewport->Size, ImGuiCond_Always);
	ImGui::SetNextWindowBgAlpha(0.61f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	const ImGuiWindowFlags dimmerFlags =
		ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_NoInputs |
		ImGuiWindowFlags_NoNav |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoBringToFrontOnFocus;
	ImGui::Begin("##PauseDimmer", nullptr, dimmerFlags);
	ImGui::End();
	ImGui::PopStyleVar(2);

	GameUi::PrepareWindow("pause", ImVec2(335, 35), ImVec2(610, 650));
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
	ImGui::Begin("ポーズ", nullptr, flags);

	if (ImGui::Button("ポーズ解除", ImVec2(-1.0f, 42.0f)))
	{
		intent.resume = true;
	}
	ImGui::TextDisabled("Escでもゲームへ戻れます");
	ImGui::SeparatorText("音量");
	bool settingsChanged = false;
	int bgmPercent = static_cast<int>(std::lround(settings.bgmVolume * 100.0f));
	int sePercent = static_cast<int>(std::lround(settings.seVolume * 100.0f));
	if (ImGui::SliderInt("BGM音量", &bgmPercent, 0, 100, "%d%%"))
	{
		settings.bgmVolume = static_cast<float>(bgmPercent) / 100.0f;
		settingsChanged = true;
	}
	if (ImGui::SliderInt("SE音量", &sePercent, 0, 100, "%d%%"))
	{
		settings.seVolume = static_cast<float>(sePercent) / 100.0f;
		settingsChanged = true;
	}
	ImGui::TextDisabled("音声実装後に利用する設定値として保存されます");

	ImGui::SeparatorText("画面遷移");
	settingsChanged |= ImGui::SliderFloat("フェードアウト時間", &settings.sceneTransition.fadeOutSeconds,
		0.0f, SceneTransitionTiming::MaximumSeconds, "%.2f 秒", ImGuiSliderFlags_AlwaysClamp);
	settingsChanged |= ImGui::SliderFloat("完全暗転の保持時間", &settings.sceneTransition.blackHoldSeconds,
		0.0f, SceneTransitionTiming::MaximumSeconds, "%.2f 秒", ImGuiSliderFlags_AlwaysClamp);
	settingsChanged |= ImGui::SliderFloat("フェードイン時間", &settings.sceneTransition.fadeInSeconds,
		0.0f, SceneTransitionTiming::MaximumSeconds, "%.2f 秒", ImGuiSliderFlags_AlwaysClamp);
	if (ImGui::Button("現在の設定でフェードをプレビュー", ImVec2(-1.0f, 34.0f)))
	{
		intent.previewSceneTransition = true;
	}
	ImGui::TextWrapped("画面を切り替えず、設定画面のまま演出を再生します。プレビュー中はEscで中断できます。");
	if (ImGui::Button("画面遷移の時間を初期値に戻す"))
	{
		settings.sceneTransition = {};
		settingsChanged = true;
	}
	ImGui::TextWrapped("次の画面遷移から反映します。タイトル開始後の暗転にも適用します。設定画面を閉じると保存されます。");
	ImGui::SeparatorText("演出効果");
	const bool vibrationWasEnabled = settings.vibrationEnabled;
	settingsChanged |= ImGui::Checkbox(
		"コントローラー振動",
		&settings.vibrationEnabled);
	settingsChanged |= ImGui::Checkbox(
		"画面フラッシュ",
		&settings.screenFlashEnabled);
	settingsChanged |= ImGui::Checkbox(
		"カメラ揺れ",
		&settings.cameraShakeEnabled);
	settingsChanged |= ImGui::Checkbox(
		"衝突強度演出",
		&settings.collisionImpactEffectsEnabled);
	settingsChanged |= ImGui::Checkbox(
		"重量CHAIN演出",
		&settings.heavyChainEffectsEnabled);
	settingsChanged |= ImGui::Checkbox(
		"敵の被弾フラッシュ",
		&settings.enemyHitFlashEnabled);
	settingsChanged |= ImGui::Checkbox(
		"撃破専用VFX",
		&settings.defeatVfxEnabled);
	settingsChanged |= ImGui::Checkbox(
		"複数撃破表示",
		&settings.multiDefeatEffectsEnabled);
	settingsChanged |= ImGui::Checkbox(
		"ショット中フレーム時間計測",
		&settings.frameTimeMeasurementEnabled);
	settingsChanged |= ImGui::SliderInt(
		"装飾リング量",
		&settings.decorativeRingAmount,
		0,
		3);
	constexpr const char* enemyHpModeLabels[] =
	{
		"従来の頭上HPバー",
		"円形HPゲージ",
		"HP非表示",
	};
	int enemyHpMode = std::clamp(
		static_cast<int>(settings.enemyHpDisplay.mode),
		0,
		IM_ARRAYSIZE(enemyHpModeLabels) - 1);
	if (ImGui::Combo(
		"敵HP表示",
		&enemyHpMode,
		enemyHpModeLabels,
		IM_ARRAYSIZE(enemyHpModeLabels)))
	{
		settings.enemyHpDisplay.mode =
			static_cast<EnemyHpDisplayMode>(enemyHpMode);
		settingsChanged = true;
	}
	if (ImGui::TreeNode("円形HPゲージ調整"))
	{
		settingsChanged |= ImGui::SliderFloat(
			"横オフセット",
			&settings.enemyHpDisplay.ringOffsetX,
			-40.0f,
			40.0f,
			"%.0f px");
		settingsChanged |= ImGui::SliderFloat(
			"縦オフセット",
			&settings.enemyHpDisplay.ringOffsetY,
			-40.0f,
			40.0f,
			"%.0f px");
		settingsChanged |= ImGui::SliderFloat(
			"直径倍率",
			&settings.enemyHpDisplay.ringDiameterScale,
			0.50f,
			1.30f,
			"%.2f");
		settingsChanged |= ImGui::SliderFloat(
			"リング太さ",
			&settings.enemyHpDisplay.ringThickness,
			2.0f,
			8.0f,
			"%.1f px");
		settingsChanged |= ImGui::SliderFloat(
			"リング不透明度",
			&settings.enemyHpDisplay.ringOpacity,
			0.35f,
			1.0f,
			"%.2f");
		constexpr const char* layerLabels[] =
		{
			"通常UIより背面",
			"通常UIより前面",
		};
		int drawLayer = std::clamp(
			static_cast<int>(settings.enemyHpDisplay.ringDrawLayer),
			0,
			IM_ARRAYSIZE(layerLabels) - 1);
		if (ImGui::Combo(
			"描画順序",
			&drawLayer,
			layerLabels,
			IM_ARRAYSIZE(layerLabels)))
		{
			settings.enemyHpDisplay.ringDrawLayer =
				static_cast<EnemyHpRingDrawLayer>(drawLayer);
			settingsChanged = true;
		}
		ImGui::TreePop();
	}
	if (vibrationWasEnabled && !settings.vibrationEnabled)
	{
		Input::SetVibration(0, 0.0f);
	}

	ImGui::SeparatorText("画面");
	settingsChanged |= ImGui::Checkbox(
		"フルスクリーン",
		&settings.fullscreen);
	if (ImGui::BeginCombo(
		"解像度",
		SettingsManager::GetResolutionLabel(settings.resolutionIndex)))
	{
		for (int index = 0;
			index < SettingsManager::GetResolutionCount();
			++index)
		{
			const bool selected = index == settings.resolutionIndex;
			if (ImGui::Selectable(
				SettingsManager::GetResolutionLabel(index),
				selected))
			{
				settings.resolutionIndex = index;
				settingsChanged = true;
			}
			if (selected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	if (ImGui::Button("画面設定を適用", ImVec2(-1.0f, 34.0f)))
	{
		intent.applyDisplay = true;
	}

	if (!model.titleScene) ImGui::SeparatorText("ランを中断");
	if (model.debugMode)
	{
		if (ImGui::Button("デバッグを終了してタイトルへ", ImVec2(-1, 40)))
			intent.requestDebugExit = true;
	}
	else if (!model.titleScene && !model.confirmReturnToTitle)
	{
		if (ImGui::Button("セーブしてタイトルへ戻る", ImVec2(-1.0f, 40.0f)))
		{
			intent.requestReturnConfirmation = true;
		}
	}
	else if (!model.titleScene)
	{
		ImGui::TextWrapped(
			"ランのセーブデータは1個だけです。現在のセーブを上書きしてタイトルへ戻ります。");
		if (model.battleScene && !model.clearRewardActive)
		{
			ImGui::TextDisabled("戦闘中のランは、この戦闘の最初から再開します。");
		}
		if (ImGui::Button("上書きして戻る", ImVec2(260.0f, 38.0f)))
		{
			intent.saveAndReturnToTitle = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("キャンセル", ImVec2(260.0f, 38.0f)))
		{
			intent.cancelReturnToTitle = true;
		}
	}

	if (settingsChanged)
	{
		intent.settingsChanged = true;
	}
	ImGui::End();
	return intent;
}

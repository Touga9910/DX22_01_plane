#include "GamePresentation.h"
#include "GameUi.h"
#include "json/json.hpp"
#include "imgui/imgui.h"

#pragma execution_character_set("utf-8")

#include <algorithm>
#include <fstream>
#include <sstream>
#include <utility>

using nlohmann::json;

namespace
{
constexpr const char* kBalanceReportPath = "logs/balance/balance_report.json";

const char* JudgementLabel(const std::string& judgement)
{
	if (judgement == "balanced") return "目標範囲内";
	if (judgement == "too_easy") return "易しすぎる傾向";
	if (judgement == "too_difficult") return "難しすぎる傾向";
	if (judgement == "insufficient_data") return "データ不足";
	return "評価待ち";
}

ImVec4 JudgementColor(const std::string& judgement)
{
	if (judgement == "balanced") return ImVec4(0.25f, 0.85f, 0.48f, 1.0f);
	if (judgement == "too_easy") return ImVec4(0.30f, 0.72f, 1.0f, 1.0f);
	if (judgement == "too_difficult") return ImVec4(1.0f, 0.43f, 0.30f, 1.0f);
	return ImVec4(0.95f, 0.78f, 0.28f, 1.0f);
}

float ReadNumber(const json& value, const char* key, float fallback = 0.0f)
{
	if (!value.is_object() || !value.contains(key) ||
		!value[key].is_number())
	{
		return fallback;
	}
	return value[key].get<float>();
}

std::string ReadString(
	const json& value,
	const char* key,
	const std::string& fallback = std::string())
{
	if (!value.is_object() || !value.contains(key) ||
		!value[key].is_string())
	{
		return fallback;
	}
	return value[key].get<std::string>();
}
}

void GamePresentation::DrawDashboard()
{
	GameUi::PrepareWindow("dashboard", ImVec2(120, 35), ImVec2(1040, 650));
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
	if (!ImGui::Begin("バランス分析ダッシュボード", &m_DashboardOpen, flags))
	{
		ImGui::End();
		return;
	}

	if (ImGui::Button("レポートを再読込"))
	{
		LoadDashboard();
	}
	ImGui::SameLine();
	ImGui::TextDisabled("F2で開閉 / %s", kBalanceReportPath);
	ImGui::Separator();
	if (!m_Dashboard.loaded)
	{
		ImGui::TextColored(
			ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
			"%s",
			m_Dashboard.error.c_str());
		ImGui::TextWrapped(
			"tools\\analyze_balance_logs.py を実行してレポートを生成してください。");
		ImGui::End();
		return;
	}

	ImGui::Text("現行バランス: %s", m_Dashboard.currentBalanceVersion.c_str());
	ImGui::SameLine();
	if (m_Dashboard.currentVersionHasData)
	{
		ImGui::TextColored(
			ImVec4(0.35f, 0.9f, 0.52f, 1.0f),
			"計測済み");
	}
	else
	{
		ImGui::TextColored(
			ImVec4(1.0f, 0.65f, 0.25f, 1.0f),
			"未計測");
	}
	if (!m_Dashboard.versions.empty())
	{
		m_Dashboard.selectedVersionIndex = std::clamp(
			m_Dashboard.selectedVersionIndex,
			0,
			static_cast<int>(m_Dashboard.versions.size()) - 1);
		const char* selectedLabel = m_Dashboard.versions[
			m_Dashboard.selectedVersionIndex].version.c_str();
			ImGui::SetNextItemWidth(230.0f);
			if (ImGui::BeginCombo("表示バージョン", selectedLabel))
			{
				for (std::size_t index = 0;
					index < m_Dashboard.versions.size();
					++index)
				{
					const DashboardVersionData& item = m_Dashboard.versions[index];
					std::string label = item.version;
					if (item.current) label += " (現行)";
					if (!item.hasData) label += " - 未計測";
					const bool selected = static_cast<int>(index) ==
						m_Dashboard.selectedVersionIndex;
					if (ImGui::Selectable(label.c_str(), selected))
					{
						m_Dashboard.selectedVersionIndex = static_cast<int>(index);
					}
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			const char* comparisonLabel = "比較なし";
			if (m_Dashboard.comparisonVersionIndex >= 0 &&
				m_Dashboard.comparisonVersionIndex <
				static_cast<int>(m_Dashboard.versions.size()))
			{
				comparisonLabel = m_Dashboard.versions[
					m_Dashboard.comparisonVersionIndex].version.c_str();
			}
			ImGui::SetNextItemWidth(230.0f);
			if (ImGui::BeginCombo("比較対象", comparisonLabel))
			{
				if (ImGui::Selectable(
					"比較なし",
					m_Dashboard.comparisonVersionIndex < 0))
				{
					m_Dashboard.comparisonVersionIndex = -1;
				}
				for (std::size_t index = 0;
					index < m_Dashboard.versions.size();
					++index)
				{
					if (static_cast<int>(index) ==
						m_Dashboard.selectedVersionIndex)
					{
						continue;
					}
					const DashboardVersionData& item = m_Dashboard.versions[index];
					std::string label = item.version;
					if (!item.hasData) label += " - 未計測";
					if (ImGui::Selectable(
						label.c_str(),
						static_cast<int>(index) ==
						m_Dashboard.comparisonVersionIndex))
					{
						m_Dashboard.comparisonVersionIndex = static_cast<int>(index);
					}
				}
				ImGui::EndCombo();
			}

			const DashboardVersionData& selectedVersion =
				m_Dashboard.versions[m_Dashboard.selectedVersionIndex];
			const DashboardVersionData* comparisonVersion = nullptr;
			if (m_Dashboard.comparisonVersionIndex >= 0 &&
				m_Dashboard.comparisonVersionIndex <
				static_cast<int>(m_Dashboard.versions.size()))
			{
				comparisonVersion = &m_Dashboard.versions[
					m_Dashboard.comparisonVersionIndex];
			}
			ImGui::TextDisabled(
				"最終計測: %s / ラン: %d / 完了ラン: %d / ショット: %d",
				selectedVersion.lastMeasuredAt.empty()
				? "未計測"
				: selectedVersion.lastMeasuredAt.c_str(),
				selectedVersion.runCount,
				selectedVersion.terminalRunCount,
				selectedVersion.totalShotCount);
			if (!selectedVersion.conditionSummary.empty())
			{
				ImGui::TextWrapped(
					"計測条件: %s",
					selectedVersion.conditionSummary.c_str());
			}
			if (!selectedVersion.hasData)
			{
				ImGui::TextColored(
					ImVec4(1.0f, 0.65f, 0.25f, 1.0f),
					"このバージョンのログはありません。過去値や仮値は表示していません。");
			}
			if (selectedVersion.smallSampleWarning && selectedVersion.hasData)
			{
				ImGui::TextColored(
					ImVec4(1.0f, 0.75f, 0.28f, 1.0f),
					"注意: 30ラン未満のため傾向判断には追加計測が必要です。");
			}
			if (selectedVersion.mixedConfigurationWarning)
			{
				ImGui::TextColored(
					ImVec4(1.0f, 0.55f, 0.25f, 1.0f),
					"同一版に複数設定があり、最新設定コホートだけを表示しています。");
			}
			if (selectedVersion.heterogeneousConditionsWarning)
			{
				ImGui::TextColored(
					ImVec4(1.0f, 0.65f, 0.25f, 1.0f),
					"計測条件が複数混在しています。版内の数値も記述統計として扱ってください。");
			}
			if (comparisonVersion != nullptr &&
				selectedVersion.hasData && comparisonVersion->hasData &&
				selectedVersion.conditionSignature !=
				comparisonVersion->conditionSignature)
			{
				ImGui::TextColored(
					ImVec4(1.0f, 0.55f, 0.25f, 1.0f),
					"比較条件が一致しません。シード・操作主体・ビルド・敵構成差を含むため、改善/悪化を断定できません。");
			}

			const auto drawMetric = [&comparisonVersion](
				const char* label,
				float value,
				bool available,
				float otherValue,
				bool otherAvailable,
				bool percentage)
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn(); ImGui::TextUnformatted(label);
					ImGui::TableNextColumn();
					if (available)
						ImGui::Text(percentage ? "%.1f%%" : "%.2f",
							percentage ? value * 100.0f : value);
					else ImGui::TextDisabled("未計測");
					ImGui::TableNextColumn();
					if (comparisonVersion == nullptr) ImGui::TextDisabled("-");
					else if (otherAvailable)
						ImGui::Text(percentage ? "%.1f%%" : "%.2f",
							percentage ? otherValue * 100.0f : otherValue);
					else ImGui::TextDisabled("未計測");
					ImGui::TableNextColumn();
					if (comparisonVersion != nullptr && available && otherAvailable)
						ImGui::Text(percentage ? "%+.1fpt" : "%+.2f",
							percentage
							? (value - otherValue) * 100.0f
							: value - otherValue);
					else ImGui::TextDisabled("-");
				};
			if (ImGui::BeginTable(
				"version_metric_table",
				4,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
			{
				ImGui::TableSetupColumn("指標");
				ImGui::TableSetupColumn(selectedVersion.version.c_str());
				ImGui::TableSetupColumn(
					comparisonVersion != nullptr
					? comparisonVersion->version.c_str()
					: "比較対象");
				ImGui::TableSetupColumn("差分");
				ImGui::TableHeadersRow();
				const DashboardVersionData empty{};
				const DashboardVersionData& other = comparisonVersion != nullptr
					? *comparisonVersion
					: empty;
				drawMetric("クリア率", selectedVersion.clearRate,
					selectedVersion.hasClearRate, other.clearRate,
					other.hasClearRate, true);
				drawMetric("平均ダメージ/ショット", selectedVersion.averageDamage,
					selectedVersion.hasAverageDamage, other.averageDamage,
					other.hasAverageDamage, false);
				drawMetric("平均ショット数/完了ラン", selectedVersion.averageShots,
					selectedVersion.hasAverageShots, other.averageShots,
					other.hasAverageShots, false);
				drawMetric("平均戦闘ターン/ステージ",
					selectedVersion.averageBattleTurns,
					selectedVersion.hasAverageBattleTurns,
					other.averageBattleTurns,
					other.hasAverageBattleTurns, false);
				drawMetric("反射後攻撃成功率",
					selectedVersion.reflectionSuccessRate,
					selectedVersion.hasReflectionSuccessRate,
					other.reflectionSuccessRate,
					other.hasReflectionSuccessRate, true);
				ImGui::EndTable();
			}
			const char* axisLabels[] = { "重量", "貫通", "反射", "Anchor" };
			if (ImGui::BeginTable(
				"axis_metric_table",
				4,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
			{
				ImGui::TableSetupColumn("軸");
				ImGui::TableSetupColumn("獲得");
				ImGui::TableSetupColumn("消費");
				ImGui::TableSetupColumn("フィニッシャー");
				ImGui::TableHeadersRow();
				for (int axis = 0; axis < 4; ++axis)
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn(); ImGui::TextUnformatted(axisLabels[axis]);
					ImGui::TableNextColumn();
					if (selectedVersion.hasAxisGained[axis])
						ImGui::Text("%d", selectedVersion.axisGained[axis]);
					else ImGui::TextDisabled("未計測");
					ImGui::TableNextColumn();
					if (selectedVersion.hasAxisConsumed[axis])
						ImGui::Text("%d", selectedVersion.axisConsumed[axis]);
					else ImGui::TextDisabled("未計測");
					ImGui::TableNextColumn();
					if (selectedVersion.hasAxisFinishers[axis])
						ImGui::Text("%d", selectedVersion.axisFinishers[axis]);
					else ImGui::TextDisabled("未計測");
				}
				ImGui::EndTable();
			}
			if (!selectedVersion.historyReason.empty())
			{
				ImGui::TextWrapped(
					"変更履歴 [%s]: %s",
					selectedVersion.historyTarget.c_str(),
					selectedVersion.historyReason.c_str());
			}
			ImGui::SeparatorText("生成時選択コホートの詳細");
	}
	if (m_Dashboard.analyzedRuns <= 0)
	{
		ImGui::TextDisabled(
			"選択コホートの計測ログがないため、詳細指標は未計測です。");
		ImGui::End();
		return;
	}

	if (ImGui::BeginTable("summary_cards", 4, ImGuiTableFlags_SizingStretchSame))
	{
		ImGui::TableNextColumn();
		ImGui::TextDisabled("総合スコア");
		ImGui::Text("%.1f / 100", m_Dashboard.balanceScore);
		ImGui::TableNextColumn();
		ImGui::TextDisabled("判定");
		ImGui::TextColored(
			JudgementColor(m_Dashboard.judgement),
			"%s",
			JudgementLabel(m_Dashboard.judgement));
		ImGui::TableNextColumn();
		ImGui::TextDisabled("分析ログ");
		ImGui::Text("%dラン", m_Dashboard.analyzedRuns);
		ImGui::TableNextColumn();
		ImGui::TextDisabled("完了ラン母数");
		ImGui::Text("%d件", m_Dashboard.balanceSamples);
		ImGui::EndTable();
	}
	ImGui::TextDisabled("生成: %s", m_Dashboard.generatedAt.c_str());
	if (!m_Dashboard.warning.empty())
	{
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.28f, 1.0f));
		ImGui::TextWrapped("注意: %s", m_Dashboard.warning.c_str());
		ImGui::PopStyleColor();
	}

	if (ImGui::BeginTabBar("dashboard_tabs"))
	{
		if (ImGui::BeginTabItem("ラン進行"))
		{
			ImGui::Spacing();
			for (const DashboardMetric& metric : m_Dashboard.metrics)
			{
				ImGui::PushID(metric.id.c_str());
				const float displayValue = metric.percentage
					? metric.value * 100.0f
					: metric.value;
				const float displayMin = metric.percentage
					? metric.targetMin * 100.0f
					: metric.targetMin;
				const float displayMax = metric.percentage
					? metric.targetMax * 100.0f
					: metric.targetMax;
				ImGui::TextColored(
					metric.inRange
					? ImVec4(0.35f, 0.9f, 0.52f, 1.0f)
					: ImVec4(1.0f, 0.48f, 0.32f, 1.0f),
					"%s",
					metric.inRange ? "OK" : "要確認");
				ImGui::SameLine(86.0f);
				if (metric.percentage)
				{
					ImGui::Text(
						"%-24s %6.1f%%   目標 %.1f?%.1f%%",
						metric.label.c_str(),
						displayValue,
						displayMin,
						displayMax);
				}
				else
				{
					ImGui::Text(
						"%-24s %6.1f   目標 %.1f?%.1f",
						metric.label.c_str(),
						displayValue,
						displayMin,
						displayMax);
				}
				const float scaleMax = (std::max)(
					metric.targetMax * 1.35f,
					metric.value * 1.1f);
				const float fraction = scaleMax > 0.0f
					? std::clamp(metric.value / scaleMax, 0.0f, 1.0f)
					: 0.0f;
				ImGui::PushStyleColor(
					ImGuiCol_PlotHistogram,
					metric.inRange
					? ImVec4(0.25f, 0.78f, 0.45f, 1.0f)
					: ImVec4(0.92f, 0.35f, 0.25f, 1.0f));
				ImGui::ProgressBar(fraction, ImVec2(-1.0f, 12.0f), "");
				ImGui::PopStyleColor();
				ImGui::PopID();
			}
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("ステージ別"))
		{
			const ImGuiTableFlags tableFlags =
				ImGuiTableFlags_Borders |
				ImGuiTableFlags_RowBg |
				ImGuiTableFlags_ScrollY |
				ImGuiTableFlags_Resizable;
			if (ImGui::BeginTable(
				"stage_table",
				7,
				tableFlags,
				ImVec2(0.0f, 390.0f)))
			{
				ImGui::TableSetupColumn("ステージ");
				ImGui::TableSetupColumn("種別");
				ImGui::TableSetupColumn("母数");
				ImGui::TableSetupColumn("勝率");
				ImGui::TableSetupColumn("中央値打数");
				ImGui::TableSetupColumn("残HP");
				ImGui::TableSetupColumn("スコア / 判定");
				ImGui::TableHeadersRow();
				for (const StageSummary& stage : m_Dashboard.stages)
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn(); ImGui::TextUnformatted(stage.id.c_str());
					ImGui::TableNextColumn(); ImGui::TextUnformatted(stage.type.c_str());
					ImGui::TableNextColumn(); ImGui::Text("%d", stage.sampleCount);
					ImGui::TableNextColumn(); ImGui::Text("%.1f%%", stage.clearRate * 100.0f);
					ImGui::TableNextColumn(); ImGui::Text("%.1f", stage.medianShots);
					ImGui::TableNextColumn(); ImGui::Text("%.1f%%", stage.remainingHpRate * 100.0f);
					ImGui::TableNextColumn();
					ImGui::TextColored(
						JudgementColor(stage.judgement),
						"%.1f  %s",
						stage.balanceScore,
						JudgementLabel(stage.judgement));
				}
				ImGui::EndTable();
			}
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("ビルド・経済・ポケット"))
		{
			ImGui::Spacing();
			ImGui::TextUnformatted("使用ボール構成");
			const float shares[2] = {
				m_Dashboard.standardShotShare,
				m_Dashboard.heavyShotShare,
			};
			ImGui::Text("スタンダード球  %.1f%%", shares[0] * 100.0f);
			ImGui::ProgressBar(shares[0], ImVec2(-1.0f, 16.0f), "");
			ImGui::Text("ヘビー球        %.1f%%", shares[1] * 100.0f);
			ImGui::ProgressBar(shares[1], ImVec2(-1.0f, 16.0f), "");
			ImGui::Separator();
			ImGui::Text(
				"戦術的ポケット使用ラン率  %.1f%%",
				m_Dashboard.tacticalPocketRate * 100.0f);
			ImGui::Text(
				"1ラン平均獲得マネー        %.1f",
				m_Dashboard.averageMoneyPerRun);
			ImGui::TextWrapped(
				"これらは因果関係ではなく記述統計です。使用率が高いボールが強いとは限らないため、プレイヤープロフィールと設定コホートを揃えて比較してください。");
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	ImGui::End();
}

void GamePresentation::LoadDashboard()
{
	m_Dashboard = DashboardData{};
	std::ifstream source(kBalanceReportPath);
	if (!source)
	{
		m_Dashboard.error =
			"分析レポートが見つかりません: " +
			std::string(kBalanceReportPath);
		return;
	}

	try
	{
		json report;
		source >> report;
		m_Dashboard.generatedAt = report.value("generated_at", "unknown");
		m_Dashboard.analyzedRuns = report.value("analyzed_file_count", 0);
		m_Dashboard.currentBalanceVersion = report.value(
			"current_balance_version",
			std::string("Unknown"));
		m_Dashboard.selectedBalanceVersion = report.value(
			"selected_balance_version",
			std::string("Legacy/Unknown"));
		m_Dashboard.latestMeasuredBalanceVersion = report.value(
			"latest_measured_balance_version",
			std::string());
		m_Dashboard.currentVersionHasData = report.value(
			"current_version_has_data",
			false);
		if (report.contains("version_catalog") &&
			report["version_catalog"].is_array())
		{
			for (const json& item : report["version_catalog"])
			{
				if (!item.is_object())
				{
					continue;
				}
				DashboardVersionData version;
				version.version = ReadString(item, "version");
				version.lastMeasuredAt = ReadString(item, "last_measured_at");
				version.configurationFingerprint = ReadString(
					item,
					"configuration_fingerprint");
				version.hasData = item.value("has_data", false);
				version.current = item.value("is_current", false);
				version.smallSampleWarning = item.value(
					"small_sample_warning",
					false);
				version.mixedConfigurationWarning = item.value(
					"mixed_configuration_warning",
					false);
				version.heterogeneousConditionsWarning = item.value(
					"heterogeneous_conditions_warning",
					false);
				version.runCount = item.value("run_count", 0);
				const json& conditions = item.value(
					"conditions",
					json::object());
				if (conditions.is_object())
				{
					version.conditionSignature = ReadString(
						conditions,
						"condition_signature");
					const auto summarizeCounts = [](const json& counts)
						{
							std::ostringstream stream;
							if (!counts.is_object()) return stream.str();
							int displayed = 0;
							for (auto iterator = counts.begin();
								iterator != counts.end() && displayed < 3;
								++iterator, ++displayed)
							{
								if (displayed > 0) stream << ", ";
								stream << iterator.key() << ':';
								if (iterator.value().is_number_integer())
									stream << iterator.value().get<int>();
								else stream << '?';
							}
							if (counts.size() > 3) stream << ", ...";
							return stream.str();
						};
					const json& controllers = conditions.value(
						"controller_types", json::object());
					const json& profiles = conditions.value(
						"controller_profiles", json::object());
					const json& builds = conditions.value(
						"build_profiles", json::object());
					const json& compositions = conditions.value(
						"stage_compositions", json::object());
					std::ostringstream conditionStream;
					conditionStream
						<< "操作[" << summarizeCounts(controllers) << "] / "
						<< "Profile[" << summarizeCounts(profiles) << "] / "
						<< "Build[" << summarizeCounts(builds) << "] / "
						<< "構成パターン " << compositions.size() << " / "
						<< "Seed記録 " << conditions.value(
							"runs_with_recorded_seeds", 0)
						<< '/' << version.runCount;
					version.conditionSummary = conditionStream.str();
				}
				const json& history = item.value(
					"history",
					json::object());
				if (history.is_object())
				{
					version.historyReason = ReadString(history, "reason");
					version.historyTarget = ReadString(history, "target");
				}
				const json& observed = item.value(
					"metrics",
					json::object());
				const auto readOptional = [](
					const json& object,
					const char* key,
					float& value,
					bool& available)
					{
						available = object.is_object() &&
							object.contains(key) &&
							object[key].is_number();
						if (available)
						{
							value = object[key].get<float>();
						}
					};
				if (observed.is_object())
				{
					version.terminalRunCount = observed.value(
						"terminal_run_count",
						0);
					version.totalShotCount = observed.value(
						"total_shot_count",
						0);
					readOptional(observed, "clear_rate",
						version.clearRate, version.hasClearRate);
					readOptional(observed, "average_damage_per_shot",
						version.averageDamage, version.hasAverageDamage);
					readOptional(observed, "average_shots_per_terminal_run",
						version.averageShots, version.hasAverageShots);
					readOptional(observed, "average_battle_turns_per_stage",
						version.averageBattleTurns,
						version.hasAverageBattleTurns);
					readOptional(observed, "reflection_success_rate",
						version.reflectionSuccessRate,
						version.hasReflectionSuccessRate);
					const json& axes = observed.value("axes", json::object());
					const char* axisNames[] = {
						"heavy", "pierce", "reflection", "anchor" };
					for (int axisIndex = 0; axisIndex < 4; ++axisIndex)
					{
						if (!axes.contains(axisNames[axisIndex]) ||
							!axes[axisNames[axisIndex]].is_object())
						{
							continue;
						}
						const json& axis = axes[axisNames[axisIndex]];
						const auto readAxisValue = [&axis](
							const char* key,
							int& value,
							bool& available)
							{
								available = axis.contains(key) && axis[key].is_number_integer();
								if (available) value = axis[key].get<int>();
							};
						readAxisValue("stacks_gained",
							version.axisGained[axisIndex],
							version.hasAxisGained[axisIndex]);
						readAxisValue("stacks_consumed",
							version.axisConsumed[axisIndex],
							version.hasAxisConsumed[axisIndex]);
						readAxisValue("finisher_activations",
							version.axisFinishers[axisIndex],
							version.hasAxisFinishers[axisIndex]);
					}
				}
				m_Dashboard.versions.push_back(std::move(version));
			}
		}
		for (std::size_t index = 0;
			index < m_Dashboard.versions.size();
			++index)
		{
			if (m_Dashboard.versions[index].version ==
				m_Dashboard.selectedBalanceVersion)
			{
				m_Dashboard.selectedVersionIndex = static_cast<int>(index);
				break;
			}
		}
		const json& runSelection = report.value(
			"run_selection",
			json::object());
		m_Dashboard.warning = runSelection.value("warning", "");

		const json& run = report.value("run_balance", json::object());
		m_Dashboard.balanceSamples = run.value("balance_sample_count", 0);
		m_Dashboard.balanceScore = ReadNumber(run, "balance_score");
		m_Dashboard.judgement = ReadString(run, "judgement");
		const json& metrics = run.value("metrics", json::object());
		const json& statuses = run.value("metric_status", json::object());
		const auto addMetric = [&](
			const char* id,
			const char* label,
			bool percentage)
			{
				DashboardMetric metric;
				metric.id = id;
				metric.label = label;
				metric.value = ReadNumber(metrics, id);
				if (statuses.contains(id) && statuses[id].is_object())
				{
					metric.targetMin = ReadNumber(statuses[id], "target_min");
					metric.targetMax = ReadNumber(statuses[id], "target_max", 1.0f);
					metric.inRange = statuses[id].value("in_range", false);
				}
				metric.percentage = percentage;
				m_Dashboard.metrics.push_back(std::move(metric));
			};
		addMetric("first_boss_reach_rate", "第1ボス到達率", true);
		addMetric("first_boss_clear_rate", "第1ボス撃破率", true);
		addMetric("second_boss_reach_rate", "第2ボス到達率", true);
		addMetric("median_reached_progress", "到達進行度中央値", false);
		addMetric("terminal_run_rate", "正常終了ラン率", true);

		const json& system = report.value("system_metrics", json::object());
		const json& build = system.value("build", json::object());
		const json& usage = build.value("ball_usage", json::object());
		const json& shares = usage.value(
			"shot_share_by_ball_id",
			json::object());
		m_Dashboard.standardShotShare = ReadNumber(
			shares,
			"player_standard");
		m_Dashboard.heavyShotShare = ReadNumber(
			shares,
			"player_heavy");
		const json& pockets = system.value("pockets", json::object());
		m_Dashboard.tacticalPocketRate = ReadNumber(
			pockets,
			"tactical_pocket_run_rate");
		const json& economy = system.value("economy", json::object());
		m_Dashboard.averageMoneyPerRun = ReadNumber(
			economy,
			"average_money_earned_per_run");

		if (report.contains("stages") && report["stages"].is_array())
		{
			for (const json& item : report["stages"])
			{
				StageSummary stage;
				stage.id = ReadString(item, "stage_id");
				stage.type = ReadString(item, "stage_type");
				stage.sampleCount = item.value("sample_count", 0);
				const json& stageMetrics = item.value(
					"metrics",
					json::object());
				stage.clearRate = ReadNumber(stageMetrics, "clear_rate");
				stage.medianShots = ReadNumber(stageMetrics, "median_shots");
				stage.remainingHpRate = ReadNumber(
					stageMetrics,
					"average_remaining_hp_ratio");
				stage.balanceScore = ReadNumber(item, "balance_score");
				stage.judgement = ReadString(item, "judgement");
				m_Dashboard.stages.push_back(std::move(stage));
			}
		}
		m_Dashboard.loaded = true;
	}
	catch (const json::exception& exception)
	{
		m_Dashboard.error =
			"分析レポートを読み込めません: " +
			std::string(exception.what());
	}
}

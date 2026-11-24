#include "GamePresentation.h"
#include "EnemyInfoLayout.h"
#include "GamePresentationPrimitives.h"
#include "CombatOverlayDrawing.h"
#include "Game.h"
#include "BattleController.h"
#include "BattleViewportLayout.h"
#include "GameRuntime.h"
#include "GameWorld.h"
#include "GameObject.h"
#include "EnemyBall.h"
#include "EnemyText.h"
#include "EnemyAttackComponent.h"
#include "PlayerBall.h"
#include "GameUi.h"
#include "imgui/imgui.h"

#pragma execution_character_set("utf-8")

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <utility>

using DirectX::SimpleMath::Vector3;
using namespace GamePresentationPrimitives;
using namespace CombatOverlayDrawing;

/// <summary>
/// 戦闘中の敵の状態、行動予告や能力の情報を盤面の表示に反映する。
/// </summary>
void GamePresentation::DrawEnemyStatusEffects(Game& game)
{
	const BattleController* battleController =
		game.GetBattleControllerDiagnostics();
	const bool phasePrototype = battleController != nullptr &&
		battleController->UsesPhaseAttackTiming();
	const BattleState state = game.GetBattleState();
	const bool showActionPlan =
		state == BattleState::AimingDirection ||
		state == BattleState::AimingPower ||
		state == BattleState::BallsMoving;
	const bool showTimedStatusEffects =
		state == BattleState::AimingDirection ||
		state == BattleState::AimingPower;
	const EnemyHpDisplaySettings& hpDisplay =
		game.GetEnemyHpDisplaySettings();
	const bool showOverheadHp =
		hpDisplay.mode == EnemyHpDisplayMode::OverheadBar;
	const bool showCircularHp =
		hpDisplay.mode == EnemyHpDisplayMode::CircularGauge;

	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNavFocus |
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
		ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus |
		ImGuiWindowFlags_NoInputs;
	const BattleViewportLayout battleLayout = BattleViewportLayout::Calculate(
		viewport->Size.x, viewport->Size.y);
	const float enemyUiSafeTop = viewport->Pos.y + battleLayout.stageTop;
	std::vector<std::pair<std::uintptr_t, ImVec4>> enemyBodyRegions;
	EnemyInfoLayout layout(viewport->Pos, viewport->Size, enemyUiSafeTop, enemyBodyRegions);

	// ============================================================
	// 点線描画：ホバー中は色を強調
	// ============================================================
	const auto drawEnemyInfoDottedLine =
		[&](const EnemyInfoLink& link, bool highlight)
		{
			const float dx = link.end.x - link.start.x;
			const float dy = link.end.y - link.start.y;
			const float length = std::sqrt(dx * dx + dy * dy);

			if (length < 0.01f)
				return;

			ImDrawList* layer = highlight
				? ImGui::GetForegroundDrawList(ImGui::GetMainViewport())
				: ImGui::GetBackgroundDrawList(ImGui::GetMainViewport());

			const ImU32 color = highlight
				? IM_COL32(255, 238, 164, 245)
				: IM_COL32(190, 215, 230, 185);

			for (float distance = 0.0f;
				distance <= length;
				distance += 5.0f)
			{
				const ImVec2 dot(
					link.start.x + dx * distance / length,
					link.start.y + dy * distance / length
				);

				layer->AddCircleFilled(
					dot,
					highlight ? 1.5f : 1.1f,
					color,
					6
				);
			}
		};

	// 重なり回避に失敗して隠れた情報がある敵に「i」を表示する。
	const auto drawHiddenEnemyInfoMarker =
		[&](const ImVec2& ballCenter, float ballRadius)
		{
			constexpr float markerRadius = 6.0f;
			const ImVec2 markerCenter(
				ballCenter.x + ballRadius * 0.78f,
				ballCenter.y + ballRadius * 0.78f
			);
			if (markerCenter.x - markerRadius < viewport->Pos.x ||
				markerCenter.y - markerRadius < viewport->Pos.y ||
				markerCenter.x + markerRadius > viewport->Pos.x + viewport->Size.x ||
				markerCenter.y + markerRadius > viewport->Pos.y + viewport->Size.y)
			{
				return;
			}

			ImDrawList* layer = ImGui::GetForegroundDrawList(ImGui::GetMainViewport());
			layer->AddCircleFilled(markerCenter, markerRadius, IM_COL32(15, 25, 31, 240), 16);
			layer->AddCircle(markerCenter, markerRadius, IM_COL32(180, 224, 239, 235), 16, 1.2f);

			const char* label = "i";
			ImFont* font = ImGui::GetFont();
			const float fontSize = 11.0f;
			const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label);
			layer->AddText(
				font, fontSize,
				ImVec2(markerCenter.x - textSize.x * 0.5f,
					markerCenter.y - textSize.y * 0.5f),
				IM_COL32(235, 248, 255, 255), label);

			const float mx = ImGui::GetIO().MousePos.x - markerCenter.x;
			const float my = ImGui::GetIO().MousePos.y - markerCenter.y;
			if (mx * mx + my * my <= markerRadius * markerRadius)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(PresentationUtf8(u8"この敵には非表示の情報があります。"));
				ImGui::TextUnformatted(PresentationUtf8(u8"敵ボールにマウスを合わせて確認できます。"));
				ImGui::EndTooltip();
			}
		};

	std::vector<ImVec4> placedEnemyPanels;
	std::vector<std::uintptr_t> liveEnemyInfoKeys;
	std::vector<ImVec4> protectedEnemyUiRegions;
	const ImVec2 workPosition = viewport->WorkPos;
	const ImVec2 workSize = viewport->WorkSize;
	protectedEnemyUiRegions.emplace_back(
		workPosition.x + 8.0f,
		workPosition.y + 8.0f,
		workPosition.x + 306.0f,
		workPosition.y + 136.0f);
	protectedEnemyUiRegions.emplace_back(
		workPosition.x + workSize.x - 210.0f,
		workPosition.y + 8.0f,
		workPosition.x + workSize.x - 8.0f,
		workPosition.y + 82.0f);
	// Ball cards occupy the bottom center, while draw/discard piles use the
	// corners. Treat the complete strip as protected so enemy intent never
	// becomes part of the card silhouette.
	protectedEnemyUiRegions.emplace_back(
		workPosition.x + 8.0f,
		workPosition.y + workSize.y - 184.0f,
		workPosition.x + workSize.x - 8.0f,
		workPosition.y + workSize.y - 4.0f);
	if (m_HitSummaryLifetime > 0.0f)
	{
		protectedEnemyUiRegions.emplace_back(
			viewport->Pos.x + viewport->Size.x - 408.0f,
			viewport->Pos.y + 256.0f,
			viewport->Pos.x + viewport->Size.x - 10.0f,
			viewport->Pos.y + 368.0f);
	}

	int visibleEnemyCount = 0;
	for (const EnemyBall* candidate :
		GameRuntime::World().GetComponents<EnemyBall>())
	{
		if (candidate != nullptr && !candidate->IsDefeated() &&
			!candidate->IsPocketed())
		{
			++visibleEnemyCount;
			ImVec2 bodyCenter;
			ImVec2 bodyTop;
			if (TryProjectToScreen(candidate->GetPosition(), bodyCenter) &&
				TryProjectToScreen(
					candidate->GetPosition() +
					Vector3(0.0f, candidate->GetVisualRadius(), 0.0f),
					bodyTop))
			{
				const float bodyRadius = (std::max)(
					14.0f,
					std::abs(bodyCenter.y - bodyTop.y));
				enemyBodyRegions.emplace_back(
					reinterpret_cast<std::uintptr_t>(candidate),
					ImVec4(
						bodyCenter.x - bodyRadius - 5.0f,
						bodyCenter.y - bodyRadius - 5.0f,
						bodyCenter.x + bodyRadius + 5.0f,
						bodyCenter.y + bodyRadius + 5.0f));
			}
		}
	}

	// 敵ボールへのマウスオーバー判定
	std::uintptr_t hoveredEnemyKey = 0;
	float nearestHoverScore = FLT_MAX;

	const ImVec2 mousePos = ImGui::GetIO().MousePos;

	for (const auto& [key, body] : enemyBodyRegions)
	{
		const float cx = (body.x + body.z) * 0.5f;
		const float cy = (body.y + body.w) * 0.5f;

		// enemyBodyRegionsは5px拡張されているため補正
		const float radius = (std::max)(
			1.0f,
			(body.z - body.x) * 0.5f - 5.0f
			);

		const float dx = mousePos.x - cx;
		const float dy = mousePos.y - cy;

		const float score =
			(dx * dx + dy * dy) / (radius * radius);

		if (score <= 1.0f && score < nearestHoverScore)
		{
			nearestHoverScore = score;
			hoveredEnemyKey = key;
		}
	}

	// Pass 0: 通常配置の計算・描画。ホバー対象も配置だけは予約しておく。
	// Pass 1: 対象の上下UIを基準位置へ戻し、最後に最前面へ重ね描画する。
	for (int uiPass = 0; uiPass < 2; ++uiPass)
	{
		const bool hoverOverlayPass = uiPass == 1;
		if (hoverOverlayPass && hoveredEnemyKey == 0)
			break;
		int enemyIndex = 0;
		for (const EnemyBall* enemy :
			GameRuntime::World().GetComponents<EnemyBall>())
		{
			const int currentIndex = enemyIndex++;
			if (enemy == nullptr || enemy->IsDefeated() || enemy->IsPocketed()) continue;
			if (hoverOverlayPass &&
				reinterpret_cast<std::uintptr_t>(enemy) != hoveredEnemyKey)
				continue;
			const auto& effects = enemy->GetStatusEffects();
			const int effectCount = showTimedStatusEffects
				? effects.GetActiveCount()
				: 0;
			const int anchorStacks = enemy->GetAnchorStacks();
			const EnemyBoardAbilityType boardAbility =
				enemy->GetBoardAbilityType();
			const bool protectedByResonator =
				enemy->IsProtectedByResonator();
			const int boardStatusCount =
				(boardAbility != EnemyBoardAbilityType::None ? 1 : 0) +
				(protectedByResonator ? 1 : 0);
			const EnemyActionType plannedAction = enemy->GetPlannedAction();
			EnemyActionType action = plannedAction;
			if (phasePrototype && action == EnemyActionType::Wait &&
				enemy->GetTurnsUntilAttack() <= 1)
			{
				if (enemy->CanUseCharge())
				{
					action = enemy->IsCharging()
						? EnemyActionType::ChargeAttack
						: EnemyActionType::ChargeStart;
				}
				else
				{
					action = EnemyActionType::NormalAttack;
				}
			}
			const bool hasActionIntent = showActionPlan && (phasePrototype ||
				(action == EnemyActionType::NormalAttack ||
				 action == EnemyActionType::ChargeStart ||
				 action == EnemyActionType::ChargeAttack));
			const bool actionIsAttack =
				action == EnemyActionType::NormalAttack ||
				action == EnemyActionType::ChargeAttack;
			int expectedDamage = 0;
			if (hasActionIntent && actionIsAttack)
			{
				const GameObject* owner = enemy->GetGameObject();
				const EnemyAttackComponent* attack = owner != nullptr
					? owner->GetComponent<EnemyAttackComponent>()
					: nullptr;
				const float displayedDamageMultiplier =
					action == EnemyActionType::ChargeAttack
						? game.GetEnemyActionConfig().chargeDamageMultiplier
						: enemy->GetPlannedDamageMultiplier();
				expectedDamage = attack != nullptr
					? PlayerBall::CalculateIncomingDamage(
						attack->CalculateDamage(
							displayedDamageMultiplier))
					: 0;
			}
			const std::string intentText = actionIsAttack
				? "-" + std::to_string(expectedDamage)
				: std::string{};
			const std::string countdownText =
				std::to_string(enemy->GetTurnsUntilAttack());
			const int shownEffectCount = visibleEnemyCount >= 10 ? (std::min)(effectCount, 1)
				: visibleEnemyCount >= 8 ? (std::min)(effectCount, 2)
				: effectCount;
			const int suppressedEffectCount = effectCount - shownEffectCount;
			const int itemCount = shownEffectCount + boardStatusCount + (anchorStacks > 0 ? 1 : 0);

			ImVec2 center;
			ImVec2 top;
			if (!TryProjectToScreen(enemy->GetPosition(), center) ||
				!TryProjectToScreen(enemy->GetPosition() + Vector3(0.0f, enemy->GetVisualRadius(), 0.0f), top))
			{
				continue;
			}
			const float projectedRadius = (std::max)(14.0f, std::abs(center.y - top.y));
			if (!hoverOverlayPass && GameUi::showDebugger && enemy->GetBoardAbilityRadius() > 0.0f)
			{
				ImVec2 rangeEdge;
				if (TryProjectToScreen(enemy->GetPosition() + Vector3(enemy->GetBoardAbilityRadius(), 0.0f, 0.0f),
					rangeEdge))
				{
					const float screenRadius = std::abs(rangeEdge.x - center.x);
					ImGui::GetBackgroundDrawList(ImGui::GetMainViewport())
						->AddCircle(center, screenRadius,
							boardAbility == EnemyBoardAbilityType::ProtectionResonator
							? IM_COL32(70, 220, 245, 105)
							: IM_COL32(255, 92, 155, 105),
							48, 1.5f);
				}
			}
			const std::uintptr_t targetKey =
				reinterpret_cast<std::uintptr_t>(enemy);

			const bool isEnemyHovered =
				targetKey == hoveredEnemyKey;
			// 他の敵をホバーしている間は、重なった通常アイコンのツールチップを抑止する。
			const bool allowEnemyIconTooltip =
				hoveredEnemyKey == 0 || isEnemyHovered;

			if (!hoverOverlayPass)
				liveEnemyInfoKeys.push_back(targetKey);

			// ホバー中の敵を最後のパスでリング強調
			if (hoverOverlayPass && isEnemyHovered)
			{
				ImGui::GetForegroundDrawList(ImGui::GetMainViewport())->AddCircle(
					center,
					projectedRadius + 3.0f,
					IM_COL32(255, 238, 164, 220),
					32,
					1.6f
				);
			}
			const auto hpVisual =
				std::find_if(m_EnemyHpVisuals.begin(), m_EnemyHpVisuals.end(),
					[targetKey](const EnemyHpVisual& value) { return value.targetKey == targetKey; });
			const int currentHp = hpVisual != m_EnemyHpVisuals.end() ? hpVisual->currentHp : (std::max)(0, enemy->GetHP());
			const int maximumHp =
				hpVisual != m_EnemyHpVisuals.end() ? hpVisual->maximumHp : (std::max)(1, enemy->GetMaxHP());
			if (hoverOverlayPass)
			{
				ImGui::SetTooltip("%s\nHP %d / %d", EnemyLabel(enemy->GetEnemyId()), currentHp, maximumHp);
			}
			const float trailingHp =
				hpVisual != m_EnemyHpVisuals.end() ? hpVisual->trailingHp : static_cast<float>(currentHp);
			const float damageHold = hpVisual != m_EnemyHpVisuals.end() ? hpVisual->damageHold : 0.0f;
			const float hpRatio = std::clamp(static_cast<float>(currentHp) / static_cast<float>(maximumHp), 0.0f, 1.0f);
			const float trailingRatio = std::clamp(trailingHp / static_cast<float>(maximumHp), 0.0f, 1.0f);
			if (!hoverOverlayPass && showCircularHp && currentHp > 0)
			{
				DrawEnemyHpRing(hpDisplay, center, projectedRadius, hpRatio, trailingRatio, damageHold);
			}

			// 上部の行動予告：通常は回避位置、ホバー中のみ敵の真上へ戻す。
			bool upperInfoHidden = false;
			if (hasActionIntent)
			{
				const ImVec2 actionBasePosition(
					center.x - kCombatIconSize * 0.5f,
					center.y - projectedRadius - kCombatIconSize - 7.0f
				);

				EnemyInfoPlacement actionPlacement{};
				if (!hoverOverlayPass)
				{
					actionPlacement = layout.Resolve(
						actionBasePosition, kCombatIconSize, kCombatIconSize,
						true, targetKey, center, projectedRadius);
					upperInfoHidden = actionPlacement.hidden;
				}
				else
				{
					// 配置処理へ再登録せず、ホバー対象だけ元の真上に重ねる。
					actionPlacement.position = layout.ClampHoverPosition(
						actionBasePosition, kCombatIconSize, kCombatIconSize);
					actionPlacement.moved =
						std::abs(actionPlacement.position.x - actionBasePosition.x) +
						std::abs(actionPlacement.position.y - actionBasePosition.y) > 0.01f;
					if (actionPlacement.moved)
						actionPlacement.link = layout.MakeLink(actionPlacement.position,
							kCombatIconSize, kCombatIconSize, true, center, projectedRadius);
				}

				const ImVec2 actionPosition = actionPlacement.position;
				const bool drawActionWindow = hoverOverlayPass
					? isEnemyHovered
					: (!isEnemyHovered && !actionPlacement.hidden);

				if (drawActionWindow && actionPlacement.moved)
					drawEnemyInfoDottedLine(actionPlacement.link, hoverOverlayPass);

				if (drawActionWindow)
				{
					char actionWindowId[64]{};
					sprintf_s(actionWindowId, "##enemy_action_intent_%d", currentIndex);
					ImGui::SetNextWindowViewport(viewport->ID);
					ImGui::SetNextWindowPos(actionPosition);
					ImGui::SetNextWindowSize(ImVec2(kCombatIconSize, kCombatIconSize));

					// 行動予告アイコンの背景・枠線
					{
						constexpr float framePadding = 3.2f;
						constexpr float frameRounding = 4.0f;

						const ImVec2 frameMin(
							actionPosition.x - framePadding,
							actionPosition.y - framePadding
						);

						const ImVec2 frameMax(
							actionPosition.x + kCombatIconSize + framePadding,
							actionPosition.y + kCombatIconSize + framePadding
						);

						ImDrawList* frameDrawList = isEnemyHovered
							? ImGui::GetForegroundDrawList(ImGui::GetMainViewport())
							: ImGui::GetBackgroundDrawList(ImGui::GetMainViewport());

						// 半透明の暗い背景
						frameDrawList->AddRectFilled(
							frameMin,
							frameMax,
							IM_COL32(12, 20, 27, isEnemyHovered ? 245 : 109),
							frameRounding
						);

						// 灰色の細い枠線
						frameDrawList->AddRect(
							frameMin,
							frameMax,
							isEnemyHovered
							? IM_COL32(255, 225, 120, 255)
							: IM_COL32(105, 128, 147, 220),
							frameRounding,
							0,
							isEnemyHovered ? 2.4f : 1.2f
						);
					}

					ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
					if (ImGui::Begin(actionWindowId, nullptr, flags))
					{
						const ImVec2 iconMinimum = ImGui::GetCursorScreenPos();
						const ImVec2 iconMaximum(iconMinimum.x + kCombatIconSize, iconMinimum.y + kCombatIconSize);
						ImGui::Dummy(ImVec2(kCombatIconSize, kCombatIconSize));
						ImDrawList* actionDrawList = isEnemyHovered
							? ImGui::GetForegroundDrawList(ImGui::GetMainViewport())
							: ImGui::GetWindowDrawList();
						DrawEnemyActionBadge(actionDrawList, iconMinimum, action);
						if (phasePrototype && action == EnemyActionType::Wait)
						{
							const char* waitLabel = "...";
							const ImVec2 waitSize = ImGui::CalcTextSize(waitLabel);
							DrawOutlinedValue(
								actionDrawList,
								ImVec2(
									iconMinimum.x +
										(kCombatIconSize - waitSize.x) * 0.5f,
									iconMinimum.y +
										(kCombatIconSize - waitSize.y) * 0.42f),
								waitLabel,
								IM_COL32(205, 220, 230, 255));
						}
						if (phasePrototype && action == EnemyActionType::Wait)
						{
							const float turnFontSize = ImGui::GetFontSize() * 0.78f;
							const ImVec2 turnSize = ImGui::GetFont()->CalcTextSizeA(
								turnFontSize, FLT_MAX, 0.0f, countdownText.c_str());
							DrawOutlinedValue(
								actionDrawList,
								ImVec2(
									iconMaximum.x - turnSize.x,
									iconMaximum.y - turnSize.y),
								countdownText.c_str(),
								IM_COL32(255, 245, 190, 255));
						}
						if (actionIsAttack)
						{
							const float damageFontSize = ImGui::GetFontSize() * 0.72f;
							const ImVec2 damageSize =
								ImGui::GetFont()->CalcTextSizeA(damageFontSize, FLT_MAX, 0.0f, intentText.c_str());
							DrawOutlinedValue(actionDrawList,
								ImVec2(iconMaximum.x - damageSize.x, iconMaximum.y - damageSize.y),
								intentText.c_str(), IM_COL32(255, 245, 218, 255));
						}
						if (allowEnemyIconTooltip && IsMouseInside(iconMinimum, iconMaximum))
						{
							ImGui::BeginTooltip();
							if (phasePrototype)
							{
								if (actionIsAttack)
								{
									ImGui::TextUnformatted(PresentationUtf8(
										u8"このターンのショット終了後に攻撃"));
								}
								else if (action == EnemyActionType::ChargeStart)
								{
									ImGui::TextUnformatted(PresentationUtf8(
										u8"このターンのショット終了後にCharge開始"));
								}
								else
								{
									ImGui::Text(PresentationUtf8(
										u8"あと%dターン後に攻撃"),
										enemy->GetTurnsUntilAttack());
								}
								ImGui::Text(PresentationUtf8(u8"攻撃間隔: %dターン"),
									enemy->GetAttackInterval());
							}
							else
							{
								ImGui::TextColored(
									ImGui::ColorConvertU32ToFloat4(EnemyActionColor(action)),
									"%s", EnemyActionDisplayName(action));
								ImGui::Text("%s: %d", PresentationUtf8(u8"行動コスト"),
									enemy->GetPlannedActionCost());
								ImGui::Text("Charge: %s",
									enemy->IsCharging() ? "Charging" : "Normal");
							}
							if (actionIsAttack)
							{
								ImGui::Text(PresentationUtf8(u8"予測ダメージ: %d（シールド適用前）"), expectedDamage);
							}
							else if (!phasePrototype &&
								action == EnemyActionType::ChargeStart)
							{
								ImGui::TextUnformatted(PresentationUtf8(u8"このターンは攻撃せずChargeを開始"));
							}
							else if (!phasePrototype)
							{
								ImGui::TextUnformatted(PresentationUtf8(u8"このターンは行動しません。"));
							}
							ImGui::EndTooltip();
						}
					}
					ImGui::End();
					ImGui::PopStyleVar();
				}
			}
			if (!showOverheadHp && itemCount <= 0)
			{
				if (!hoverOverlayPass && !isEnemyHovered && upperInfoHidden)
					drawHiddenEnemyInfoMarker(center, projectedRadius);
				continue;
			}
			const float rowWidth = (shownEffectCount + boardStatusCount) * kCombatIconSize +
				(anchorStacks > 0 ? kResourceBadgeWidth : 0.0f) +
				(std::max)(0, itemCount - 1) * kCombatIconSpacing;
			const float panelWidth = showOverheadHp ? (std::max)(92.0f, rowWidth) : (std::max)(1.0f, rowWidth);
			const float panelHeight =
				showOverheadHp ? (itemCount > 0 ? 45.0f : 18.0f) : (itemCount > 0 ? kCombatIconSize : 0.0f);
			// 敵の真下を基準位置とする
			const ImVec2 panelBasePosition(
				center.x - panelWidth * 0.5f,
				center.y + projectedRadius + 6.0f
			);

			EnemyInfoPlacement panelPlacement{};
			if (!hoverOverlayPass)
			{
				panelPlacement = layout.Resolve(
					panelBasePosition, panelWidth, panelHeight,
					false, targetKey, center, projectedRadius);
			}
			else
			{
				// ホバー表示では通常配置を変えず、真下の基準位置に戻す。
				panelPlacement.position = layout.ClampHoverPosition(
					panelBasePosition, panelWidth, panelHeight);
				panelPlacement.moved =
					std::abs(panelPlacement.position.x - panelBasePosition.x) +
					std::abs(panelPlacement.position.y - panelBasePosition.y) > 0.01f;
				if (panelPlacement.moved)
					panelPlacement.link = layout.MakeLink(panelPlacement.position,
						panelWidth, panelHeight, false, center, projectedRadius);
			}

			// 省略されたウィンドウがある敵だけ、通常描画時に小さな i を付ける。
			if (!hoverOverlayPass && !isEnemyHovered &&
				(upperInfoHidden || panelPlacement.hidden))
				drawHiddenEnemyInfoMarker(center, projectedRadius);

			const bool drawLowerWindow = hoverOverlayPass
				? isEnemyHovered
				: (!isEnemyHovered && !panelPlacement.hidden);
			if (!drawLowerWindow)
				continue;

			const ImVec2 panelPosition = panelPlacement.position;

			if (panelPlacement.moved)
				drawEnemyInfoDottedLine(panelPlacement.link, hoverOverlayPass);
			// パネル全体が画面外の場合のみ描画をスキップする。
			// 画面内では位置をずらさない。
			if (panelPosition.x + panelWidth < viewport->Pos.x ||
				panelPosition.x > viewport->Pos.x + viewport->Size.x ||
				panelPosition.y + panelHeight < viewport->Pos.y ||
				panelPosition.y > viewport->Pos.y + viewport->Size.y)
			{
				continue;
			}

			char windowId[64]{};
			sprintf_s(windowId, "##enemy_status_effects_%d", currentIndex);
			ImGui::SetNextWindowViewport(viewport->ID);
			ImGui::SetNextWindowPos(panelPosition);
			ImGui::SetNextWindowSize(ImVec2(panelWidth, panelHeight));

			// バフ・デバフ一覧の背景・枠線
			{
				constexpr float framePadding = 3.2f;
				constexpr float frameRounding = 4.0f;

				const ImVec2 frameMin(
					panelPosition.x - framePadding,
					panelPosition.y - framePadding
				);

				const ImVec2 frameMax(
					panelPosition.x + panelWidth + framePadding,
					panelPosition.y + panelHeight + framePadding
				);

				ImDrawList* frameDrawList = isEnemyHovered
					? ImGui::GetForegroundDrawList(ImGui::GetMainViewport())
					: ImGui::GetBackgroundDrawList(ImGui::GetMainViewport());

				// 半透明の暗い背景
				frameDrawList->AddRectFilled(
					frameMin,
					frameMax,
					IM_COL32(12, 20, 27, isEnemyHovered ? 245 : 109),
					frameRounding
				);

				// 灰色の細い枠線
				frameDrawList->AddRect(
					frameMin,
					frameMax,
					isEnemyHovered
					? IM_COL32(255, 225, 120, 255)
					: IM_COL32(105, 128, 147, 220),
					frameRounding,
					0,
					isEnemyHovered ? 2.4f : 1.2f
				);
			}

			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(kCombatIconSpacing, 0.0f));
			if (ImGui::Begin(windowId, nullptr, flags))
			{
				ImDrawList* drawList = isEnemyHovered
					? ImGui::GetForegroundDrawList(ImGui::GetMainViewport())
					: ImGui::GetWindowDrawList();
				if (showOverheadHp)
				{
					const ImVec2 hpMinimum(
						panelPosition.x,
						panelPosition.y);
					const ImVec2 hpMaximum(
						panelPosition.x + panelWidth,
						panelPosition.y + 16.0f);
					drawList->AddRectFilled(
						hpMinimum,
						hpMaximum,
						IM_COL32(8, 12, 18, 235),
						4.0f);
					drawList->AddRectFilled(
						hpMinimum,
						ImVec2(
							hpMinimum.x + panelWidth * trailingRatio,
							hpMaximum.y),
						IM_COL32(255, 154, 54, 225),
						4.0f);
					const ImU32 hpColor = EnemyHpColor(hpRatio, 1.0f);
					drawList->AddRectFilled(
						hpMinimum,
						ImVec2(
							hpMinimum.x + panelWidth * hpRatio,
							hpMaximum.y),
						hpColor,
						4.0f);
					drawList->AddRect(
						hpMinimum,
						hpMaximum,
						IM_COL32(232, 240, 250, 230),
						4.0f,
						0,
						1.2f);
					char hpText[48]{};
					sprintf_s(hpText, "HP %d / %d", currentHp, maximumHp);
					const ImVec2 hpTextSize = ImGui::CalcTextSize(hpText);
					drawList->AddText(
						ImVec2(
							hpMinimum.x + (panelWidth - hpTextSize.x) * 0.5f,
							hpMinimum.y + (16.0f - hpTextSize.y) * 0.5f),
						IM_COL32(255, 255, 255, 255),
						hpText);
					ImGui::Dummy(ImVec2(panelWidth, 16.0f));
				}
				if (itemCount > 0)
				{
					ImGui::SetCursorScreenPos(ImVec2(
						panelPosition.x + (panelWidth - rowWidth) * 0.5f,
						panelPosition.y + (showOverheadHp ? 22.0f : 0.0f)));
				}
				int drawn = 0;
				const auto drawBoardBadge =
					[&drawn, drawList, allowEnemyIconTooltip](CombatIconKind icon, ImU32 color, const char* title, const std::string& detail)
					{
						if (drawn++ > 0)
							ImGui::SameLine();
						const ImVec2 minimum = ImGui::GetCursorScreenPos();
						const ImVec2 maximum(minimum.x + kCombatIconSize, minimum.y + kCombatIconSize);
						ImGui::Dummy(ImVec2(kCombatIconSize, kCombatIconSize));
						DrawCombatIcon(drawList, icon, minimum, maximum);
						if (allowEnemyIconTooltip && IsMouseInside(minimum, maximum))
						{
							ImGui::BeginTooltip();
							ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(color), "%s", title);
							ImGui::TextUnformatted(detail.c_str());
							ImGui::EndTooltip();
						}
					};
				if (boardAbility == EnemyBoardAbilityType::SwarmAmplifier)
				{
					drawBoardBadge(CombatIconKind::EnemySwarm, IM_COL32(255, 86, 155, 255), PresentationUtf8(u8"群体強化"),
						std::string(PresentationUtf8(u8"周囲の生存中の味方 ")) +
						std::to_string(enemy->GetNearbyLivingAllyCount()) +
						PresentationUtf8(u8"体 / 強化段階 ") + std::to_string(enemy->GetSwarmStage() + 1) +
						PresentationUtf8(u8" / 現在攻撃力 ") + std::to_string(enemy->GetAttack()));
				}
				else if (boardAbility == EnemyBoardAbilityType::ImpactBrittle)
				{
					drawBoardBadge(CombatIconKind::EnemyBrittle,
						enemy->IsBrittle() ? IM_COL32(120, 220, 255, 255) : IM_COL32(255, 158, 55, 255),
						PresentationUtf8(enemy->IsBrittle() ? u8"衝撃脆化中" : u8"衝撃脆化球"),
						enemy->IsBrittle() ? PresentationUtf8(u8"次の攻撃1回は攻撃力が低下します。")
						: PresentationUtf8(u8"強く壁へ衝突させると脆化します。"));
				}
				else if (boardAbility == EnemyBoardAbilityType::ProtectionResonator)
				{
					drawBoardBadge(CombatIconKind::EnemyResonance, IM_COL32(64, 218, 238, 255),
						PresentationUtf8(u8"防護共鳴球"),
						PresentationUtf8(u8"範囲内の他の味方が受けるダメージを1軽減します。"));
				}
				if (protectedByResonator)
				{
					drawBoardBadge(CombatIconKind::EnemyResonance, IM_COL32(92, 232, 248, 255),
						PresentationUtf8(u8"防護共鳴中"),
						PresentationUtf8(u8"受けるダメージが1軽減されます。効果は重複しません。"));
				}
				int drawnStatusEffects = 0;
				for (const StatusEffectType effectType : AllStatusEffectTypes)
				{
					if (!showTimedStatusEffects)
						break;
					const int magnitude = effects.GetMagnitude(effectType);
					if (magnitude <= 0)
						continue;
					if (drawnStatusEffects >= shownEffectCount)
						break;
					if (drawn++ > 0)
						ImGui::SameLine();
					++drawnStatusEffects;
					const ImVec2 iconMinimum = ImGui::GetCursorScreenPos();
					const ImVec2 iconMaximum(iconMinimum.x + kCombatIconSize, iconMinimum.y + kCombatIconSize);
					ImGui::Dummy(ImVec2(kCombatIconSize, kCombatIconSize));
					DrawStatusIcon(drawList, iconMinimum, effectType, magnitude);
					if (drawnStatusEffects == shownEffectCount && suppressedEffectCount > 0)
					{
						char hiddenCount[12]{};
						sprintf_s(hiddenCount, "+%d", suppressedEffectCount);
						const float hiddenFontSize = ImGui::GetFontSize() * 0.68f;
						const ImVec2 hiddenSize =
							ImGui::GetFont()->CalcTextSizeA(hiddenFontSize, FLT_MAX, 0.0f, hiddenCount);
						const ImVec2 hiddenMinimum(iconMaximum.x - hiddenSize.x, iconMaximum.y - hiddenSize.y);
						DrawOutlinedValue(drawList, hiddenMinimum, hiddenCount, IM_COL32(240, 244, 250, 255), 0.68f);
					}
					const ImVec2 mouse = ImGui::GetIO().MousePos;
					const bool hovered = mouse.x >= iconMinimum.x && mouse.x < iconMaximum.x && mouse.y >= iconMinimum.y &&
						mouse.y < iconMaximum.y;
					if (hovered && allowEnemyIconTooltip)
					{
						const bool attack = IsAttackStatusEffect(effectType);
						const int signedMagnitude = IsPositiveStatusEffect(effectType) ? magnitude : -magnitude;
						const int baseValue = attack ? enemy->GetStatus().attack : enemy->GetStatus().defense;
						const int currentValue = attack ? enemy->GetAttack() : enemy->GetDefense();
						ImGui::BeginTooltip();
						ImGui::TextColored(attack ? ImVec4(1.0f, 0.35f, 0.31f, 1.0f) : ImVec4(0.32f, 0.62f, 1.0f, 1.0f),
							"%s", StatusEffectName(effectType));
						ImGui::Text("現在の効果：%s %+d", attack ? "攻撃力" : "防御力", signedMagnitude);
						ImGui::Text("基礎値 %d  →  現在値 %d", baseValue, currentValue);
						ImGui::EndTooltip();
					}
				}
				if (anchorStacks > 0)
				{
					if (drawn++ > 0)
						ImGui::SameLine();
					const ImVec2 iconMinimum = ImGui::GetCursorScreenPos();
					const ImVec2 iconMaximum(iconMinimum.x + kResourceBadgeWidth, iconMinimum.y + kResourceBadgeHeight);
					ImGui::Dummy(ImVec2(kResourceBadgeWidth, kResourceBadgeHeight));
					DrawResourceBadge(drawList, iconMinimum,
						{ CombatIconKind::ResourceEnemyAnchor, PresentationUtf8(u8"敵の錨スタック"),
						 PresentationUtf8(u8"この敵が個別に保持する資源です。"), anchorStacks,
						 IM_COL32(75, 225, 183, 255) });
					if (allowEnemyIconTooltip && IsMouseInside(iconMinimum, iconMaximum))
					{
						ImGui::BeginTooltip();
						ImGui::TextColored(ImVec4(0.29f, 0.88f, 0.72f, 1.0f), "%s", PresentationUtf8(u8"敵の錨スタック"));
						ImGui::Text(PresentationUtf8(u8"現在値: %d"), anchorStacks);
						ImGui::TextUnformatted(PresentationUtf8(u8"この敵が個別に保持する資源です。"));
						ImGui::EndTooltip();
					}
				}
			}
			ImGui::End();
			ImGui::PopStyleVar(2);
		}
	}
	m_EnemyInfoLayouts.erase(
		std::remove_if(
			m_EnemyInfoLayouts.begin(),
			m_EnemyInfoLayouts.end(),
			[&liveEnemyInfoKeys](const EnemyInfoLayoutVisual& value)
			{
				return std::find(
					liveEnemyInfoKeys.begin(),
					liveEnemyInfoKeys.end(),
					value.targetKey) == liveEnemyInfoKeys.end();
			}),
		m_EnemyInfoLayouts.end());

	DrawPlayerResourceBadges(game);
}

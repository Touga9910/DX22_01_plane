#include "Game.h"
#include "Renderer.h"
#include "BallPhysicsComponent.h"
#include "input.h"

#include "PlayerBall.h"  // DrawImGui蜻ｼ縺ｳ蜃ｺ縺励↓蠢・ｦ・
#include "EnemyBall.h"   // DrawImGui蜻ｼ縺ｳ蜃ｺ縺励↓蠢・ｦ・
#include "EnemyAttackComponent.h"
#include "BallComponent.h"
#include "PlayerBallDataLoader.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_win32.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <random>

Game* Game::m_Instance;//繧ｲ繝ｼ繝繧､繝ｳ繧ｹ繧ｿ繝ｳ繧ｹ

namespace
{
	enum class RewardTargetType
	{
		SingleBall,
		PlayerOverall,
		Exit,
	};

	struct RewardDefinition
	{
		const char* name;
		RewardTargetType targetType;
		int cost;
	};

	constexpr RewardDefinition kRewardDefinitions[] =
	{
		{ "Attack Up +1",        RewardTargetType::SingleBall,    10 },
		{ "Defense Up +1",       RewardTargetType::SingleBall,    10 },
		{ "Heal 3 HP",           RewardTargetType::PlayerOverall,  5 },
		{ "Player Max HP Up +1", RewardTargetType::PlayerOverall, 15 },
		{ "Leave",               RewardTargetType::Exit,           0 },
	};

	constexpr int kRewardCount =
		static_cast<int>(
			sizeof(kRewardDefinitions) /
			sizeof(kRewardDefinitions[0])
			);

	BallStatus NormalizeBallStatus(BallStatus status)
	{
		status.maxHp = (std::max)(1, status.maxHp);
		status.mass = (std::max)(0.0001f, status.mass);
		status.radius = (std::max)(0.0f, status.radius);
		status.restitution = std::clamp(status.restitution, 0.0f, 1.0f);
		status.friction = (std::max)(0.0f, status.friction);

		return status;
	}

	PlayerRunStatus NormalizePlayerRunStatus(PlayerRunStatus status)
	{
		status.maxHp = (std::max)(1, status.maxHp);
		status.currentHp = std::clamp(status.currentHp, 0, status.maxHp);

		return status;
	}

	const char* GetGameStateDebugName(GameState state)
	{
		switch (state)
		{
		case GameState::AimingDirection: return "AimingDirection";
		case GameState::AimingPower:     return "AimingPower";
		case GameState::ConfirmShot:     return "ConfirmShot";
		case GameState::BallsMoving:     return "BallsMoving";
		case GameState::EnemyAttack:     return "EnemyAttack";
		case GameState::TurnEnd:         return "TurnEnd";
		case GameState::ClearReward:     return "ClearReward";
		case GameState::GameOver:        return "GameOver";
		default:                         return "Unknown";
		}
	}

	const char* GetSceneDebugName(Scene* scene)
	{
		if (dynamic_cast<TitleScene*>(scene)) return "TITLE";
		if (dynamic_cast<StageSelectScene*>(scene)) return "SELECT";
		if (dynamic_cast<Stage1Scene*>(scene)) return "STAGE1";
		if (dynamic_cast<Stage2Scene*>(scene)) return "STAGE2";
		if (dynamic_cast<Stage3Scene*>(scene)) return "STAGE3";
		if (dynamic_cast<ResultScene*>(scene)) return "RESULT";

		return "Unknown";
	}

	void WriteVector3(std::ofstream& file, const char* label, const DirectX::SimpleMath::Vector3& value)
	{
		file << label << " = ("
			<< value.x << ", "
			<< value.y << ", "
			<< value.z << ")\n";
	}

	void WriteBallDebugStatus(std::ofstream& file, const char* typeName, int index, BallComponent* ball)
	{
		if (ball == nullptr)
		{
			return;
		}

		const Transform transform = ball->GetMutableTransform();

		file << "[" << typeName << " " << index << "]\n";
		WriteVector3(file, "Position", transform.position);
		WriteVector3(file, "Velocity", ball->GetVelocity());
		file << "HP = " << ball->GetHP() << " / " << ball->GetMaxHP() << "\n";
		file << "Radius = " << ball->GetRadius() << "\n";
		file << "IsStopped = " << (ball->IsStopped() ? "true" : "false") << "\n";
		file << "IsDefeated = " << (ball->IsDefeated() ? "true" : "false") << "\n";
		file << "\n";
	}
}

// 繧ｳ繝ｳ繧ｹ繝医Λ繧ｯ繧ｿ
Game::Game()
{
	m_Scene = nullptr;
}

// 繝・せ繝医Λ繧ｯ繧ｿ
Game::~Game()
{
	delete m_Scene;
	DeleteAllObject();
}

// 蛻晄悄蛹・
void Game::Init()
{
	// 髱咏噪繧､繝ｳ繧ｹ繧ｿ繝ｳ繧ｹ繧偵％縺薙〒1縺､縺縺醍函謌・
	if (m_Instance == nullptr) {
		m_Instance = new Game();
	}
	// 謠冗判邨ゆｺ・・逅・
	Renderer::Init();

	//蜈･蜉帛・逅・・譛溷喧
	Input::Create();

	// 繧ｫ繝｡繝ｩ蛻晄悄蛹・
	m_Instance->m_Camera.Init();

	m_Instance->LoadPlayerStatusFromJson();

	//譛蛻昴・繧ｷ繝ｼ繝ｳ繧定ｪｭ縺ｿ縺薙・
	m_Instance->m_Scene = new TitleScene;
}

// 譖ｴ譁ｰ
void Game::Update()
{
	// 蜈･蜉帛・逅・峩譁ｰ
	Input::Update();

	// ==========================
	// ClearReward荳ｭ縺ｯ繧ｲ繝ｼ繝譛ｬ邱ｨ繧呈峩譁ｰ縺励↑縺・
	// ==========================
	if (m_Instance->m_GameState == GameState::ClearReward)
	{
		m_Instance->UpdateClearReward();
		return;
	}

	if (m_Instance->m_PlayerDeck.GetOfferCount() > 0)
	{
		m_Instance->UpdateBallSelection();
	}

	//繧ｷ繝ｼ繝ｳ譖ｴ譁ｰ
	m_Instance->m_Scene->Update();

	// 繧ｫ繝｡繝ｩ譖ｴ譁ｰ
	m_Instance->m_Camera.Update();

	for (auto& gameObject : m_Instance->m_GameObjects)
	{
		gameObject->FixedUpdate();
	}

	//繧ｪ繝悶ず繧ｧ繧ｯ繝域峩譁ｰ
	for (auto& gameObject : m_Instance->m_GameObjects)
	{
		gameObject->Update();
	}

	for (auto& gameObject : m_Instance->m_GameObjects)
	{
		gameObject->LateUpdate();
	}
	// 豁ｻ莠｡繝輔Λ繧ｰ・・P縺・縺ｪ縺ｩ・峨′遶九▲縺ｦ縺・ｋ繧ｪ繝悶ず繧ｧ繧ｯ繝医ｒ閾ｪ蜍輔・蜍慕噪蜑企勁
	std::erase_if(m_Instance->m_GameObjects, [](const std::unique_ptr<GameObject>& o) {
		if (o && o->IsDead()) {
			o->Uninit(); // 蜑企勁縺輔ｌ繧句燕縺ｫ邨ゆｺ・・逅・ｒ蜻ｼ縺ｶ
			return true; // 驟榊・縺九ｉ蜑企勁縺吶ｋ
		}
		return false;    // 谿九☆
		});

	// 隕∫ｴ縺梧ｸ帙▲縺溷ｴ蜷医・繝｡繝｢繝ｪ繧定ｩｰ繧√ｋ・域里蟄倥・蜃ｦ逅・ｒ縺薙％縺ｫ髮・ｴ・ｼ・
	m_Instance->m_GameObjects.shrink_to_fit();

	// 繧ｲ繝ｼ繝迥ｶ諷九・譖ｴ譁ｰ・亥・繝懊・繝ｫ蛛懈ｭ｢讀懷・・・
	switch (m_Instance->m_GameState)
	{
	case GameState::BallsMoving:
		if (m_Instance->AreAllBallsStopped())
		{
			if (m_Instance->AreAllEnemiesDefeated())
			{
				m_Instance->StartClearReward();

				// 謾ｻ謦・・縺励↑縺・・縺ｧ繝ｪ繧ｿ繝ｼ繝ｳ
				return;
			}

			m_Instance->m_GameState = GameState::EnemyAttack;
		}
		break;

	case GameState::EnemyAttack:
		m_Instance->ProcessEnemyAttack();
		break;

	case GameState::TurnEnd:
		m_Instance->PrepareNextPlayerBall();
		break;

	case GameState::GameOver:
		m_Instance->ProcessGameOver();
		break;

	default:
		break;
	}
}

// 謠冗判
/*
void Game::Draw()
{
	SkyBox* sky = Game::GetInstance()->GetSkyBox();
	if (sky)
		sky->Draw(&m_Instance->m_Camera);

	Renderer::DrawStart();

	for (auto& gameObject : m_Instance->m_GameObjects)
		gameObject->Draw();

	// 笘・ImGui縺ｮBegin?End繧奪rawEnd()縺ｮ蜑阪↓遘ｻ蜍・
	ImGui::Begin("Ball Debugger");

	std::vector<PlayerBall*> players = m_Instance->GetObjects<PlayerBall>();
	for (int i = 0; i < players.size(); i++)
		players[i]->DrawImGui();

	std::vector<EnemyBall*> enemies = m_Instance->GetObjects<EnemyBall>();
	for (int i = 0; i < enemies.size(); i++)
	{
		std::string label = "EnemyBall " + std::to_string(i);
		enemies[i]->DrawImGui(label);
	}

	ImGui::End();

	// ==========================
	// 蝣ｱ驟ｬUI繧呈怙蠕後↓驥阪・繧・
	// ==========================
	if (m_Instance->m_GameState == GameState::ClearReward)
	{
		m_Instance->DrawClearRewardUI();
	}

	// Render 縺ｨ RenderDrawData 繧・DrawEnd()縺ｮ蜑阪↓遘ｻ蜍・
	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

	Renderer::DrawEnd(); // 竊・Present 縺ｯ譛蠕・
}
*/

void Game::Draw()
{
	SkyBox* sky = Game::GetInstance()->GetSkyBox();
	if (sky)
	{
		sky->Draw(&m_Instance->m_Camera);
	}

	Renderer::DrawStart();

	for (auto& gameObject : m_Instance->m_GameObjects)
	{
		gameObject->Draw();
	}

	ImGui::Begin("Ball Debugger");

	ImGui::Text("GameState = %d", static_cast<int>(m_Instance->m_GameState));

	ImGui::Text("AreAllBallsStopped = %s",
		m_Instance->AreAllBallsStopped() ? "true" : "false");

	ImGui::Text("AreAllEnemiesDefeated = %s",
		m_Instance->AreAllEnemiesDefeated() ? "true" : "false");

	ImGui::Text("Player Run HP = %d / %d",
		m_Instance->m_PlayerRunStatus.currentHp,
		m_Instance->m_PlayerRunStatus.maxHp);
	ImGui::Text("Draw Pile Count = %d", m_Instance->GetPlayerDeckCount());
	ImGui::Text("Discard Pile Count = %d", m_Instance->GetPlayerDiscardCount());
	ImGui::Text("Offer Count = %d", m_Instance->m_PlayerDeck.GetOfferCount());
	ImGui::Text("Total Deck Count = %d", m_Instance->m_PlayerDeck.GetRewardTargetCount());
	ImGui::Text("Current Ball Used = %s",
		m_Instance->m_PlayerDeck.IsCurrentUsed() ? "true" : "false");

	const PlayerBallData* currentDebugBall =
		m_Instance->m_PlayerDeck.GetCurrent();
	if (currentDebugBall != nullptr)
	{
		ImGui::Text(
			"Current Ball ID = %s",
			currentDebugBall->definitionId.c_str()
		);

		ImGui::Text(
			"Current Ball Attack = %d",
			currentDebugBall->status.attack
		);
	}
	else
	{
		ImGui::Text("Current Ball ID = none");
	}

	if (ImGui::Button("Save Debug Snapshot"))
	{
		m_Instance->SaveDebugSnapshot();
	}
	ImGui::SameLine();
	ImGui::TextUnformatted("debug_state_snapshot.txt");

	std::vector<PlayerBall*> players = m_Instance->GetObjects<PlayerBall>();
	for (int i = 0; i < players.size(); i++)
	{
		players[i]->DrawImGui();
	}

	std::vector<EnemyBall*> enemies = m_Instance->GetObjects<EnemyBall>();
	for (int i = 0; i < enemies.size(); i++)
	{
		std::string label = "EnemyBall " + std::to_string(i);
		enemies[i]->DrawImGui(label);
	}

	ImGui::Text(
		"Player Money = %d",
		m_Instance->m_PlayerRunStatus.money
	);

	ImGui::End();

	if (m_Instance->m_PlayerDeck.GetOfferCount() > 0)
	{
		m_Instance->DrawBallSelectionUI();
	}

	// ==========================
	// 蝣ｱ驟ｬUI繧呈怙蠕後↓驥阪・繧・
	// ==========================
	if (m_Instance->m_GameState == GameState::ClearReward)
	{
		m_Instance->DrawClearRewardUI();
	}

	// ImGui縺ｮ謠冗判蜀・ｮｹ繧堤｢ｺ螳・
	ImGui::Render();

	// DirectX11縺ｧImGui繧呈緒逕ｻ
	ImGui_ImplDX11_RenderDrawData(
		ImGui::GetDrawData()
	);

	// 譛蠕後↓逕ｻ髱｢繧定｡ｨ遉ｺ
	Renderer::DrawEnd();
}

// 邨ゆｺ・・逅・
void Game::Uninit()
{
	// 繧ｫ繝｡繝ｩ邨ゆｺ・・逅・
	m_Instance->m_Camera.Uninit();

	//繧ｪ繝悶ず繧ｧ繧ｯ繝育ｵゆｺ・・逅・
	for (auto& o : m_Instance->m_GameObjects)
	{
		o->Uninit();;
	}


	//蜈･蜉帛・逅・ｵゆｺ・
	Input::Release();

	// 謠冗判邨ゆｺ・・逅・
	Renderer::Uninit();

	//繧､繝ｳ繧ｹ繧ｿ繝ｳ繧ｹ蜑企勁
	delete m_Instance;
}

//繧､繝ｳ繧ｹ繧ｿ繝ｳ繧ｹ蜿門ｾ・
Game* Game::GetInstance()
{
	return m_Instance;
}

GameObject* Game::CreateGameObject(const std::string& name)
{
	auto gameObject = std::make_unique<GameObject>(name);
	GameObject* result = gameObject.get();
	m_GameObjects.emplace_back(std::move(gameObject));
	return result;
}

//繧ｷ繝ｼ繝ｳ蛻・ｊ譖ｿ縺・
void Game::ChangeScene(SceneName sName)
{
	int score = 0;

	if (m_Instance->m_Scene != nullptr)
	{
		m_Instance->CaptureCurrentPlayerStatus();

		if (Stage1Scene* sObj =
			dynamic_cast<Stage1Scene*>(m_Instance->m_Scene))
		{
			score = sObj->GetScore();
		}

		delete m_Instance->m_Scene;
		m_Instance->m_Scene = nullptr;
	}

	// =====================================
	// 譁ｰ縺励＞繧ｹ繝・・繧ｸ縺ｸ蜈･繧九→縺榊ｱ驟ｬ蜿門ｾ礼憾諷九ｒ繝ｪ繧ｻ繝・ヨ
	// =====================================
	if (sName == STAGE1 ||
		sName == STAGE2 ||
		sName == STAGE3)
	{
		// ステージ開始時に現在ボール・山札・捨て札を回収して再シャッフルする。
		m_PlayerDeck.Reset();
		BeginBallSelection();

		m_IsStageRewardCollected = false;
		m_CurrentStageRewardMoney = 0;
		m_RewardMessage.clear();
	}

	switch (sName)
	{
	case TITLE:
		m_Instance->m_Scene = new TitleScene;
		break;

	case STAGE1:
		m_Instance->m_Scene = new Stage1Scene;
		break;

	case STAGE2:
		m_Instance->m_Scene = new Stage2Scene;
		break;

	case STAGE3:
		m_Instance->m_Scene = new Stage3Scene;
		break;

	case SELECT:
		m_Instance->m_Scene = new StageSelectScene;
		break;

	case RESULT:
		m_Instance->m_Scene = new ResultScene;

		dynamic_cast<ResultScene*>(
			m_Instance->m_Scene
			)->SetScore(score);

		break;

	default:
		break;
	}
}

//繧ｪ繝悶ず繧ｧ繧ｯ繝医ｒ蜑企勁
void Game::DeleteObject(Object* pt)
{
	DeleteComponent(pt);
}

void Game::DeleteComponent(Component* component)
{
	if (component == nullptr) return;

	// 1. 縺ｾ縺壹∵悽蠖薙↓ m_Objects 縺ｮ荳ｭ縺ｫ pt 縺悟ｭ伜惠縺吶ｋ縺狗｢ｺ隱阪☆繧・
	GameObject* owner = component->GetGameObject();
	auto it = std::find_if(m_Instance->m_GameObjects.begin(), m_Instance->m_GameObjects.end(),
		[owner](const std::unique_ptr<GameObject>& element) {
			return element.get() == owner;
		});

	// 2. 蟄伜惠縺励↑縺・ｼ医☆縺ｧ縺ｫ豸医∴縺ｦ縺・ｋ・峨↑繧我ｽ輔ｂ縺励↑縺・
	if (it == m_Instance->m_GameObjects.end()) return;

	// 3. 蟄伜惠縺吶ｋ蝣ｴ蜷医・縺ｿ縲∝ｮ牙・縺ｫ邨ゆｺ・＠縺ｦ蜑企勁
	m_Instance->m_GameObjects.erase(it);

	// 窶ｻ邨ゆｺ・凾縺ｮ繝ｫ繝ｼ繝嶺ｸｭ縺ｫ shrink_to_fit() 繧帝ｫ倬ｻ蠎ｦ縺ｧ蜻ｼ縺ｶ縺ｨ繝｡繝｢繝ｪ蜀咲｢ｺ菫昴〒關ｽ縺｡繧・☆縺・◆繧√・
	// 蜑企勁蜃ｦ逅・・逶ｴ蠕後〒縺ｯ縺ｪ縺上√ご繝ｼ繝蜈ｨ菴薙・Update縺ｮ譛蠕後↑縺ｩ縺ｧ蜻ｼ縺ｶ縺ｮ縺悟ｮ牙・縺ｧ縺吶・
}

//繧ｪ繝悶ず繧ｧ繧ｯ繝医ｒ蜈ｨ蜑企勁
void Game::DeleteAllObject()
{
	//邨ゆｺ・・逅・
	for (auto& o : m_Instance->m_GameObjects)
	{
		o->Uninit();
	}
	m_Instance->m_GameObjects.clear();
	m_Instance->m_GameObjects.shrink_to_fit();
}

SkyBox* Game::GetSkyBox()
{
	{
		if (m_Instance)
		{
			return m_Instance->m_SkyBox;
		}
		return nullptr;
	}
}

bool Game::ContainsObject(const Object* pt) const
{
	return ContainsComponent(pt);
}

bool Game::ContainsComponent(const Component* component) const
{
	if (component == nullptr) return false;

	for (const auto& gameObject : m_GameObjects)
	{
		if (gameObject.get() == component->GetGameObject())
		{
			return true;
		}
	}

	return false;
}

bool Game::AreAllEnemiesDefeated() const
{
	std::vector<EnemyBall*> enemies = m_Instance->GetObjects<EnemyBall>();

	// 謨ｵ縺・菴薙ｂ縺・↑縺・ｴ蜷医・繧ｯ繝ｪ繧｢謇ｱ縺・↓縺励↑縺・
	if (enemies.empty())
	{
		return false;
	}

	for (EnemyBall* enemy : enemies)
	{
		if (enemy == nullptr)
		{
			continue;
		}

		if (!enemy->IsDefeated())
		{
			return false;
		}
	}

	return true;
}

bool Game::AreAllBallsStopped() const
{
	std::vector<GameObject*> balls = m_Instance->GetGameObjectsWith<BallPhysicsComponent>();

	if (balls.empty())
	{
		return false;
	}

	// Game::AreAllBallsStopped()
	for (GameObject* ball : balls)
	{
		if (ball == nullptr) continue;

		// 謦・ｴ貂医∩繝懊・繝ｫ縺ｯ蛛懈ｭ｢蛻､螳壹°繧蛾勁螟・
		BallStatusComponent* status = ball->GetComponent<BallStatusComponent>();
		if (status != nullptr && status->IsDefeated()) continue;

		BallPhysicsComponent* physics = ball->GetComponent<BallPhysicsComponent>();
		if (physics != nullptr && !physics->IsStopped())
		{
			return false;
		}
	}

	return true;
}

void Game::ProcessEnemyAttack()
{
	std::vector<PlayerBall*> players = GetObjects<PlayerBall>();
	std::vector<EnemyBall*> enemies = GetObjects<EnemyBall>();

	if (players.empty())
	{
		m_GameState = GameState::GameOver;
		return;
	}

	PlayerBall* player = players[0];

	for (EnemyBall* enemy : enemies)
	{
		if (enemy == nullptr)
		{
			continue;
		}

		if (enemy->IsDefeated())
		{
			continue;
		}

		EnemyAttackComponent* attack =
			enemy->GetGameObject()->GetComponent<EnemyAttackComponent>();
		if (attack != nullptr)
		{
			attack->Attack(player);
		}
	}

	CapturePlayerStatusFrom(player);

	if (m_PlayerRunStatus.currentHp <= 0)
	{
		m_GameState = GameState::GameOver;
	}
	else
	{
		m_GameState = GameState::TurnEnd;
	}
}

void Game::ProcessGameOver()
{
	DiscardCurrentPlayerBall();
	ChangeScene(RESULT);
	m_GameState = GameState::AimingDirection;
}

void Game::StartClearReward()
{
	DiscardCurrentPlayerBall();

	// 謨ｵ蜈ｨ貊・ｱ驟ｬ繧呈園謖｀oney縺ｸ蜉邂励☆繧・
	CollectStageRewardMoney();

	m_SelectedRewardIndex = 0;
	m_SelectedRewardBallIndex = 0;
	m_GameState = GameState::ClearReward;
}

void Game::UpdateClearReward()
{
	if (Input::GetKeyTrigger(VK_UP))
	{
		m_SelectedRewardIndex--;

		if (m_SelectedRewardIndex < 0)
		{
			m_SelectedRewardIndex = kRewardCount - 1;
		}
	}

	if (Input::GetKeyTrigger(VK_DOWN))
	{
		m_SelectedRewardIndex++;

		if (m_SelectedRewardIndex >= kRewardCount)
		{
			m_SelectedRewardIndex = 0;
		}
	}

	const RewardDefinition& selectedReward =
		kRewardDefinitions[m_SelectedRewardIndex];

	// 繝懊・繝ｫ蠑ｷ蛹悶ｒ驕ｸ繧薙〒縺・ｋ縺ｨ縺阪□縺大ｯｾ雎｡繝懊・繝ｫ繧貞､画峩縺吶ｋ
	if (selectedReward.targetType ==
		RewardTargetType::SingleBall)
	{
		const int ballCount =
			m_PlayerDeck.GetRewardTargetCount();

		if (ballCount > 0)
		{
			if (Input::GetKeyTrigger(VK_LEFT))
			{
				m_SelectedRewardBallIndex--;

				if (m_SelectedRewardBallIndex < 0)
				{
					m_SelectedRewardBallIndex =
						ballCount - 1;
				}
			}

			if (Input::GetKeyTrigger(VK_RIGHT))
			{
				m_SelectedRewardBallIndex++;

				if (m_SelectedRewardBallIndex >= ballCount)
				{
					m_SelectedRewardBallIndex = 0;
				}
			}
		}
		else
		{
			m_SelectedRewardBallIndex = 0;
		}
	}

	if (!Input::GetKeyTrigger(VK_SPACE))
	{
		return;
	}

	// Leave繧帝∈謚槭＠縺溷ｴ蜷医・繧ｷ繝ｧ繝・・繧堤ｵゆｺ・☆繧・
	if (selectedReward.targetType ==
		RewardTargetType::Exit)
	{
		ChangeScene(SELECT);
		m_GameState = GameState::AimingDirection;
		return;
	}

	// 驕ｸ謚樔ｸｭ縺ｮ蠑ｷ蛹悶・蝗槫ｾｩ繧定ｳｼ蜈･縺吶ｋ
	TryPurchaseReward(m_SelectedRewardIndex);
}

void Game::BeginBallSelection()
{
	if (!m_PlayerDeck.PrepareOffer())
	{
		m_GameState = GameState::GameOver;
		return;
	}

	m_SelectedOfferIndex = 0;
	m_SelectedHoldIndex = -1;

	// 前回から保持していたボールは、初期状態では保持を継続する。
	for (int index = 0; index < m_PlayerDeck.GetOfferCount(); index++)
	{
		if (m_PlayerDeck.WasHeldOffer(index))
		{
			m_SelectedHoldIndex = index;
			break;
		}
	}

	if (m_SelectedHoldIndex >= 0 && m_PlayerDeck.GetOfferCount() > 1)
	{
		// 保持中のボールとは別の、新しく引いた候補を初期選択にする。
		m_SelectedOfferIndex = 1;
	}
	else if (m_SelectedHoldIndex == m_SelectedOfferIndex)
	{
		m_SelectedHoldIndex = -1;
	}

	m_GameState = GameState::AimingDirection;
	ApplySelectedBallPreview();
}

void Game::UpdateBallSelection()
{
	const int offerCount = m_PlayerDeck.GetOfferCount();
	if (offerCount <= 0)
	{
		return;
	}

	bool selectionChanged = false;
	for (int index = 0; index < offerCount && index < 3; index++)
	{
		if (Input::GetKeyTrigger('1' + index))
		{
			m_SelectedOfferIndex = index;
			selectionChanged = true;
			if (m_SelectedHoldIndex == index)
			{
				m_SelectedHoldIndex = -1;
			}
		}
	}

	constexpr int HOLD_KEYS[] = { 'Q', 'W', 'E' };
	for (int index = 0; index < offerCount && index < 3; index++)
	{
		if (!Input::GetKeyTrigger(HOLD_KEYS[index]) ||
			index == m_SelectedOfferIndex)
		{
			continue;
		}

		m_SelectedHoldIndex =
			m_SelectedHoldIndex == index ? -1 : index;
	}

	if (selectionChanged)
	{
		ApplySelectedBallPreview();
	}
}

void Game::ApplySelectedBallPreview()
{
	std::vector<PlayerBall*> players = GetObjects<PlayerBall>();
	if (players.empty() || players[0] == nullptr)
	{
		return;
	}

	ApplyPlayerStatusTo(players[0]);
}

void Game::DrawBallSelectionUI()
{
	ImGui::SetNextWindowPos(
		ImVec2(30.0f, 90.0f),
		ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(
		ImVec2(520.0f, 430.0f),
		ImGuiCond_FirstUseEver);

	ImGuiWindowFlags flags =
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoResize;

	ImGui::Begin("Ball Selection", nullptr, flags);
	ImGui::TextUnformatted("Choose a ball, then aim and shoot normally.");
	ImGui::TextUnformatted("The selected ball is applied immediately.");
	ImGui::TextUnformatted("You may hold one of the other balls.");
	ImGui::Separator();

	const int offerCount = m_PlayerDeck.GetOfferCount();
	for (int index = 0; index < offerCount; index++)
	{
		const PlayerBallData* ball = m_PlayerDeck.GetOffer(index);
		if (ball == nullptr)
		{
			continue;
		}

		ImGui::PushID(index);
		ImGui::Text(
			"[%d] %s%s",
			index + 1,
			ball->definitionId.c_str(),
			m_PlayerDeck.WasHeldOffer(index) ? "  (HELD)" : "");
		ImGui::Text(
			"ATK:%d  DEF:%d  MASS:%.2f  RADIUS:%.2f",
			ball->status.attack,
			ball->status.defense,
			ball->status.mass,
			ball->status.radius);
		ImGui::Text(
			"Split:%s  Pierce:%s",
			ball->status.abilities.split ? "Yes" : "No",
			ball->status.abilities.pierce ? "Yes" : "No");

		if (ImGui::RadioButton(
			"Use",
			m_SelectedOfferIndex == index))
		{
			m_SelectedOfferIndex = index;
			if (m_SelectedHoldIndex == index)
			{
				m_SelectedHoldIndex = -1;
			}
			ApplySelectedBallPreview();
		}

		ImGui::SameLine();
		if (m_SelectedOfferIndex == index)
		{
			ImGui::TextUnformatted("Selected for this shot");
		}
		else
		{
			const bool isHeld = m_SelectedHoldIndex == index;
			if (ImGui::Button(isHeld ? "Release Hold" : "Hold"))
			{
				m_SelectedHoldIndex = isHeld ? -1 : index;
			}
		}

		ImGui::Separator();
		ImGui::PopID();
	}

	ImGui::TextUnformatted("1 / 2 / 3 : Use ball");
	ImGui::TextUnformatted("Q / W / E : Toggle hold");
	ImGui::TextUnformatted("The choice is finalized when the shot is fired.");
	ImGui::End();
}

void Game::DrawClearRewardUI()
{
	ImGui::SetNextWindowPos(
		ImVec2(300.0f, 120.0f),
		ImGuiCond_Always
	);

	ImGui::SetNextWindowSize(
		ImVec2(600.0f, 420.0f),
		ImGuiCond_Always
	);

	ImGuiWindowFlags flags =
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoCollapse;

	ImGui::Begin("Clear Reward UI", nullptr, flags);

	ImGui::Text("CLEAR!");
	ImGui::Separator();

	ImGui::Text(
		"Stage Reward : +%d Money",
		m_CurrentStageRewardMoney
	);

	ImGui::Text(
		"Current Money : %d",
		m_PlayerRunStatus.money
	);

	ImGui::Text(
		"Player HP : %d / %d",
		m_PlayerRunStatus.currentHp,
		m_PlayerRunStatus.maxHp
	);

	ImGui::Separator();
	ImGui::Text("Shop");

	for (int i = 0; i < kRewardCount; i++)
	{
		const RewardDefinition& reward =
			kRewardDefinitions[i];

		const char* mark =
			i == m_SelectedRewardIndex
			? ">"
			: " ";

		if (reward.targetType ==
			RewardTargetType::Exit)
		{
			ImGui::Text(
				"%s [ %s ]",
				mark,
				reward.name
			);
		}
		else
		{
			ImGui::Text(
				"%s [ %s ]  Cost:%d",
				mark,
				reward.name,
				reward.cost
			);
		}
	}

	const RewardDefinition& selectedReward =
		kRewardDefinitions[m_SelectedRewardIndex];

	// 繝懊・繝ｫ蠑ｷ蛹悶ｒ驕ｸ謚樔ｸｭ縺ｮ蝣ｴ蜷医□縺大ｯｾ雎｡繧定｡ｨ遉ｺ
	if (selectedReward.targetType ==
		RewardTargetType::SingleBall)
	{
		ImGui::Separator();
		ImGui::Text("Target Ball");

		const int ballCount =
			m_PlayerDeck.GetRewardTargetCount();

		for (int i = 0; i < ballCount; i++)
		{
			const PlayerBallData* ball =
				m_PlayerDeck.GetRewardTarget(i);

			if (ball == nullptr)
			{
				continue;
			}

			const char* mark =
				i == m_SelectedRewardBallIndex
				? ">"
				: " ";

			ImGui::Text(
				"%s [%d] %s  ATK:%d DEF:%d",
				mark,
				i,
				ball->definitionId.c_str(),
				ball->status.attack,
				ball->status.defense
			);
		}

		if (ballCount <= 0)
		{
			ImGui::Text("No target ball");
		}
	}

	ImGui::Separator();

	if (!m_RewardMessage.empty())
	{
		ImGui::Text("%s", m_RewardMessage.c_str());
	}

	ImGui::Separator();
	ImGui::Text("UP / DOWN : Select");
	ImGui::Text("LEFT / RIGHT : Target Ball");
	ImGui::Text("SPACE : Buy / Leave");

	ImGui::End();
}

bool Game::ApplyReward(int rewardIndex)
{
	if (rewardIndex < 0 ||
		rewardIndex >= kRewardCount)
	{
		return false;
	}

	const RewardDefinition& reward =
		kRewardDefinitions[rewardIndex];

	switch (reward.targetType)
	{
	case RewardTargetType::SingleBall:
	{
		PlayerBallData* targetBall =
			m_PlayerDeck.GetRewardTarget(
				m_SelectedRewardBallIndex
			);

		if (targetBall == nullptr)
		{
			m_RewardMessage = "No target ball";
			return false;
		}

		switch (rewardIndex)
		{
		case 0:
			targetBall->status.attack += 1;
			break;

		case 1:
			targetBall->status.defense += 1;
			break;

		default:
			return false;
		}

		// 迴ｾ蝨ｨ菴ｿ逕ｨ荳ｭ縺ｮ繝懊・繝ｫ縺ｪ繧牙ｮ滉ｽ薙↓繧ょ渚譏縺吶ｋ
		PlayerBallData* currentBall =
			m_PlayerDeck.GetCurrent();

		if (targetBall == currentBall)
		{
			std::vector<PlayerBall*> players =
				GetObjects<PlayerBall>();

			if (!players.empty())
			{
				ApplyPlayerStatusTo(players[0]);
			}
		}

		return true;
	}

	case RewardTargetType::PlayerOverall:
	{
		switch (rewardIndex)
		{
		case 2:
		{
			constexpr int HEAL_AMOUNT = 3;

			if (m_PlayerRunStatus.currentHp >=
				m_PlayerRunStatus.maxHp)
			{
				m_RewardMessage = "HP is already full";
				return false;
			}

			m_PlayerRunStatus.currentHp += HEAL_AMOUNT;
			break;
		}

		case 3:
		{
			constexpr int MAX_HP_UP_AMOUNT = 1;

			m_PlayerRunStatus.maxHp +=
				MAX_HP_UP_AMOUNT;

			// 譛螟ｧHP荳頑・蛻・□縺醍樟蝨ｨHP繧ょ｢怜刈
			m_PlayerRunStatus.currentHp +=
				MAX_HP_UP_AMOUNT;
			break;
		}

		default:
			return false;
		}

		m_PlayerRunStatus =
			NormalizePlayerRunStatus(m_PlayerRunStatus);

		// 迴ｾ蝨ｨ陦ｨ遉ｺ荳ｭ縺ｮPlayerBall縺ｫ繧ょ渚譏縺吶ｋ
		std::vector<PlayerBall*> players =
			GetObjects<PlayerBall>();

		if (!players.empty())
		{
			ApplyPlayerRunStatusTo(players[0]);
		}

		return true;
	}

	case RewardTargetType::Exit:
	default:
		return false;
	}
}

void Game::LoadPlayerStatusFromJson(
	const std::string& filePath,
	const std::string& deckFilePath)
{
	PlayerBallDataLoadResult loadResult =
		PlayerBallDataLoader::Load(
			filePath,
			m_DefaultPlayerStatus,
			m_DefaultPlayerRunStatus
		);

	m_DefaultPlayerStatus = loadResult.defaultBallStatus;
	m_DefaultPlayerRunStatus = NormalizePlayerRunStatus(
		loadResult.defaultRunStatus
	);

	std::vector<PlayerBallData> defaultDeck =
		PlayerBallDataLoader::LoadDeck(
			deckFilePath,
			loadResult.ballDefinitions
		);
	m_PlayerDeck.SetDefaultDeck(defaultDeck);

	ResetPlayerRuntimeStatus();
}
void Game::ResetPlayerRuntimeStatus()
{
	m_PlayerRunStatus = NormalizePlayerRunStatus(m_DefaultPlayerRunStatus);
	m_PlayerDeck.Reset();
}
void Game::ApplyPlayerStatusTo(PlayerBall* player)
{
	if (player == nullptr)
	{
		return;
	}

	const PlayerBallData* selectedBall = m_PlayerDeck.GetCurrent();
	if (selectedBall == nullptr)
	{
		selectedBall = m_PlayerDeck.GetOffer(m_SelectedOfferIndex);
	}

	if (selectedBall == nullptr)
	{
		ApplyPlayerRunStatusTo(player);
		return;
	}

	player->SetStatus(selectedBall->status);

	// 物理半径だけでなく、描画モデルの大きさも選択したボールへ合わせる。
	if (selectedBall->status.radius > 0.0f && player->GetBall() != nullptr)
	{
		const float visualScale = selectedBall->status.radius;
		Transform& transform = player->GetBall()->GetMutableTransform();
		transform.scale = DirectX::SimpleMath::Vector3(
			visualScale,
			visualScale,
			visualScale);
		player->GetBall()->SynchronizeComponents();
	}

	ApplyPlayerRunStatusTo(player);
}

void Game::ApplyPlayerRunStatusTo(PlayerBall* player)
{
	if (player == nullptr)
	{
		return;
	}

	m_PlayerRunStatus = NormalizePlayerRunStatus(m_PlayerRunStatus);
	player->SetMaxHP(m_PlayerRunStatus.maxHp);
	player->SetHP(m_PlayerRunStatus.currentHp);
}
void Game::CapturePlayerStatusFrom(const PlayerBall* player)
{
	if (player == nullptr)
	{
		return;
	}

	BallStatus updatedStatus =
		NormalizeBallStatus(player->GetStatus());

	PlayerBallData* currentBall = m_PlayerDeck.GetCurrent();
	if (currentBall != nullptr)
	{
		const int ballMaxHp = currentBall->status.maxHp;
		currentBall->status = updatedStatus;
		currentBall->status.maxHp = ballMaxHp;
	}

	m_PlayerRunStatus = NormalizePlayerRunStatus(m_PlayerRunStatus);
	m_PlayerRunStatus.currentHp = std::clamp(
		player->GetHP(),
		0,
		m_PlayerRunStatus.maxHp
	);
}
void Game::CaptureCurrentPlayerStatus()
{
	std::vector<PlayerBall*> players = GetObjects<PlayerBall>();
	if (players.empty())
	{
		return;
	}
	CapturePlayerStatusFrom(players[0]);
}

void Game::DrawNextPlayerBall()
{
	m_PlayerDeck.DrawNext();
}
void Game::PrepareNextPlayerBall()
{
	DiscardCurrentPlayerBall();

	if (m_PlayerDeck.HasCurrent())
	{
		return;
	}

	BeginBallSelection();
}

int Game::CalculateStageRewardMoney() const
{
	int totalRewardMoney = 0;

	std::vector<EnemyBall*> enemies =
		m_Instance->GetObjects<EnemyBall>();

	for (EnemyBall* enemy : enemies)
	{
		if (enemy == nullptr)
		{
			continue;
		}

		// 蛟偵＠縺滓雰縺ｮ蝣ｱ驟ｬ縺縺代ｒ蜿門ｾ励☆繧・
		if (!enemy->IsDefeated())
		{
			continue;
		}

		totalRewardMoney +=
			(std::max)(0, enemy->GetRewardMoney());
	}

	return totalRewardMoney;
}

void Game::CollectStageRewardMoney()
{
	// StartClearReward縺瑚､・焚蝗槫他縺ｰ繧後※繧ゆｺ碁㍾蜿門ｾ励＠縺ｪ縺・
	if (m_IsStageRewardCollected)
	{
		return;
	}

	m_CurrentStageRewardMoney =
		CalculateStageRewardMoney();

	m_PlayerRunStatus.money +=
		m_CurrentStageRewardMoney;

	m_PlayerRunStatus =
		NormalizePlayerRunStatus(m_PlayerRunStatus);

	m_IsStageRewardCollected = true;

	m_RewardMessage =
		"Stage Reward: +" +
		std::to_string(m_CurrentStageRewardMoney) +
		" Money";
}

bool Game::TryPurchaseReward(int rewardIndex)
{
	if (rewardIndex < 0 ||
		rewardIndex >= kRewardCount)
	{
		return false;
	}

	const RewardDefinition& reward =
		kRewardDefinitions[rewardIndex];

	if (reward.targetType == RewardTargetType::Exit)
	{
		return false;
	}

	// 謇謖｀oney縺瑚ｶｳ繧翫↑縺・
	if (m_PlayerRunStatus.money < reward.cost)
	{
		m_RewardMessage = "Not enough Money";
		return false;
	}

	// HP貅繧ｿ繝ｳ縺ｪ縺ｩ縺ｧ驕ｩ逕ｨ縺ｧ縺阪↑縺九▲縺溷ｴ蜷医・Money繧呈ｸ帙ｉ縺輔↑縺・
	if (!ApplyReward(rewardIndex))
	{
		return false;
	}

	m_PlayerRunStatus.money -= reward.cost;
	m_PlayerRunStatus =
		NormalizePlayerRunStatus(m_PlayerRunStatus);

	m_RewardMessage =
		std::string(reward.name) +
		" purchased";

	return true;
}

void Game::OnPlayerShotFired(PlayerBall* player)
{
	// 選択内容はショットした瞬間に確定する。
	if (!m_PlayerDeck.HasCurrent())
	{
		if (!m_PlayerDeck.SelectOffer(
			m_SelectedOfferIndex,
			m_SelectedHoldIndex))
		{
			return;
		}
	}

	if (player != nullptr)
	{
		CapturePlayerStatusFrom(player);
	}

	m_PlayerDeck.MarkCurrentUsed();
}

void Game::DiscardCurrentPlayerBall()
{
	if (!m_PlayerDeck.HasCurrent())
	{
		m_PlayerDeck.ClearCurrentUsed();
		return;
	}

	if (!m_PlayerDeck.IsCurrentUsed())
	{
		return;
	}

	std::vector<PlayerBall*> players = GetObjects<PlayerBall>();
	if (!players.empty() && players[0] != nullptr)
	{
		CapturePlayerStatusFrom(players[0]);
	}

	m_PlayerDeck.DiscardCurrentIfUsed();
}

void Game::SaveDebugSnapshot()
{
	std::ofstream file("debug_state_snapshot.txt");

	if (!file)
	{
		return;
	}

	file << std::fixed << std::setprecision(3);

	file << "[Scene]\n";
	file << "Scene = " << GetSceneDebugName(m_Scene) << "\n";
	file << "GameState = " << GetGameStateDebugName(m_GameState)
		<< " (" << static_cast<int>(m_GameState) << ")\n";
	file << "AreAllBallsStopped = " << (AreAllBallsStopped() ? "true" : "false") << "\n";
	file << "AreAllEnemiesDefeated = " << (AreAllEnemiesDefeated() ? "true" : "false") << "\n";
	file << "\n";

	std::vector<PlayerBall*> players = GetObjects<PlayerBall>();
	std::vector<EnemyBall*> enemies = GetObjects<EnemyBall>();

	file << "[BallCounts]\n";
	file << "PlayerRunCurrentHp = "
		<< m_PlayerRunStatus.currentHp << "\n";

	file << "PlayerRunMaxHp = "
		<< m_PlayerRunStatus.maxHp << "\n";

	file << "[Deck]\n";
	file << "DrawPile = "
		<< m_PlayerDeck.GetDrawPileCount() << "\n";

	file << "DiscardPile = "
		<< m_PlayerDeck.GetDiscardPileCount() << "\n";

	file << "OfferCount = "
		<< m_PlayerDeck.GetOfferCount() << "\n";

	file << "TotalDeckCount = "
		<< m_PlayerDeck.GetRewardTargetCount() << "\n";

	const PlayerBallData* currentBall = m_PlayerDeck.GetCurrent();
	if (currentBall != nullptr)
	{
		file << "CurrentBallId = "
			<< currentBall->definitionId
			<< "\n";

		file << "CurrentBallAttack = "
			<< currentBall->status.attack
			<< "\n";

		file << "CurrentBallUsed = "
			<< (m_PlayerDeck.IsCurrentUsed() ? "true" : "false")
			<< "\n";
	}
	else
	{
		file << "CurrentBallId = none\n";
	}

	file << "\n";
	file << "PlayerBall = " << players.size() << "\n";
	file << "EnemyBall = " << enemies.size() << "\n";
	file << "\n";

	file << "PlayerMoney = "
		<< m_PlayerRunStatus.money
		<< "\n";

	for (int i = 0; i < static_cast<int>(players.size()); i++)
	{
		WriteBallDebugStatus(file, "PlayerBall", i, players[i]->GetBall());
	}

	for (int i = 0; i < static_cast<int>(enemies.size()); i++)
	{
		WriteBallDebugStatus(file, "EnemyBall", i, enemies[i]->GetBall());
	}
}

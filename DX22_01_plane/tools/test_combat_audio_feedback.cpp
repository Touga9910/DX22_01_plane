#include "../AudioFeedback.h"
#include "../GameEvent.h"
#include "../PlayerBallData.h"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <vector>

namespace
{
	void WriteU16(std::ofstream& stream, std::uint16_t value)
	{
		const char bytes[2] = {
			static_cast<char>(value & 0xff),
			static_cast<char>((value >> 8) & 0xff),
		};
		stream.write(bytes, 2);
	}

	void WriteU32(std::ofstream& stream, std::uint32_t value)
	{
		const char bytes[4] = {
			static_cast<char>(value & 0xff),
			static_cast<char>((value >> 8) & 0xff),
			static_cast<char>((value >> 16) & 0xff),
			static_cast<char>((value >> 24) & 0xff),
		};
		stream.write(bytes, 4);
	}

	void WriteTestWav(const std::filesystem::path& path)
	{
		constexpr std::uint32_t SampleRate = 48000;
		constexpr std::uint16_t Channels = 1;
		constexpr std::uint16_t Bits = 16;
		const std::vector<std::int16_t> samples(SampleRate / 20, 0);
		const std::uint32_t dataBytes = static_cast<std::uint32_t>(
			samples.size() * sizeof(std::int16_t));
		std::ofstream stream(path, std::ios::binary | std::ios::trunc);
		stream.write("RIFF", 4);
		WriteU32(stream, 36 + dataBytes);
		stream.write("WAVEfmt ", 8);
		WriteU32(stream, 16);
		WriteU16(stream, 1);
		WriteU16(stream, Channels);
		WriteU32(stream, SampleRate);
		WriteU32(stream, SampleRate * Channels * Bits / 8);
		WriteU16(stream, Channels * Bits / 8);
		WriteU16(stream, Bits);
		stream.write("data", 4);
		WriteU32(stream, dataBytes);
		stream.write(
			reinterpret_cast<const char*>(samples.data()),
			dataBytes);
		assert(stream.good());
	}
}

int main()
{
	int voiceTotal = 0;
	for (std::size_t index = 0;
		index < static_cast<std::size_t>(CombatSeCategory::Count);
		++index)
	{
		const CombatSeCategory category =
			static_cast<CombatSeCategory>(index);
		voiceTotal += CombatAudioRules::Get(category).maximumVoices;
		for (const auto strength : {
			CollisionPresentationRules::Strength::Weak,
			CollisionPresentationRules::Strength::Medium,
			CollisionPresentationRules::Strength::Strong })
		{
			const float volume = CombatAudioRules::Volume(
				category, strength, 8.0f, 10);
			const float pitch = CombatAudioRules::Pitch(
				category, strength, 10);
			assert(std::isfinite(volume));
			assert(volume >= CombatAudioRules::Get(category).minimumVolume);
			assert(volume <= CombatAudioRules::Get(category).maximumVolume);
			assert(pitch >= CombatAudioRules::MinimumPitch);
			assert(pitch <= CombatAudioRules::MaximumPitch);
		}
	}
	assert(voiceTotal == CombatAudioRules::MaximumVoices);
	assert(CombatAudioRules::SanitizeSpeed(
		std::numeric_limits<float>::quiet_NaN()) == 0.0f);
	assert(CombatAudioRules::SanitizeSpeed(-5.0f) == 0.0f);
	assert(CombatAudioRules::SanitizeSpeed(999.0f) ==
		CollisionPresentationRules::MaximumPresentationSpeed);
	assert(CombatAudioRules::Priority(
		CombatSeCategory::ChainMilestone,
		CollisionPresentationRules::Strength::Medium) >
		CombatAudioRules::Priority(
			CombatSeCategory::EnemyImpact,
			CollisionPresentationRules::Strength::Strong));

	AudioFeedback audio;
	audio.Update(1.0 / 60.0, 0.8f);
	audio.OnShotFired(ShotFiredEvent{
		"player_heavy", static_cast<int>(BallCategory::Heavy) });
	CollisionImpactEvent enemyImpact{};
	enemyImpact.collisionKind = CombatCollisionKind::EnemyEnemy;
	enemyImpact.relativeApproachSpeed = 6.5f;
	enemyImpact.shotBallCategory = static_cast<int>(BallCategory::Heavy);
	for (int collision = 0; collision < 11; ++collision)
	{
		audio.OnCollisionImpact(enemyImpact, collision + 1);
	}

	CollisionImpactEvent directImpact{};
	directImpact.collisionKind = CombatCollisionKind::PlayerEnemy;
	directImpact.relativeApproachSpeed = 4.0f;
	audio.OnCollisionImpact(directImpact, 11);

	for (std::uintptr_t target = 1; target <= 3; ++target)
	{
		EnemyDamageEvent defeat{};
		defeat.defeated = true;
		defeat.targetKey = target;
		audio.OnEnemyDamage(defeat);
	}
	EnemyDamageEvent duplicateDefeat{};
	duplicateDefeat.defeated = true;
	duplicateDefeat.targetKey = 2;
	audio.OnEnemyDamage(duplicateDefeat);

	WallImpactEvent wall{};
	wall.relativeApproachSpeed = 3.0f;
	audio.OnWallImpact(wall);

	const AudioFeedbackDiagnostics& diagnostics = audio.Diagnostics();
	assert(diagnostics.categoryRequests[CombatAudioRules::Index(
		CombatSeCategory::EnemyImpact)] == 11);
	assert(diagnostics.categoryRequests[CombatAudioRules::Index(
		CombatSeCategory::ChainMilestone)] == 3);
	assert(diagnostics.categoryRequests[CombatAudioRules::Index(
		CombatSeCategory::DirectImpact)] == 1);
	assert(diagnostics.categoryRequests[CombatAudioRules::Index(
		CombatSeCategory::EnemyDefeat)] == 3);
	assert(diagnostics.categoryRequests[CombatAudioRules::Index(
		CombatSeCategory::WallImpact)] == 1);
	assert(diagnostics.duplicateDefeatsSuppressed == 1);
	assert(diagnostics.requests == 19);
	assert(diagnostics.played == 0);
	assert(diagnostics.deviceUnavailableSkipped == diagnostics.requests);

	AudioFeedback chain15;
	chain15.Update(1.0 / 60.0, 0.8f);
	chain15.OnShotFired(ShotFiredEvent{
		"player_heavy", static_cast<int>(BallCategory::Heavy) });
	for (int collision = 0; collision < 15; ++collision)
	{
		chain15.OnCollisionImpact(enemyImpact, collision + 1);
	}
	assert(chain15.Diagnostics().categoryRequests[CombatAudioRules::Index(
		CombatSeCategory::ChainMilestone)] == 4);
	for (const std::uint64_t requests :
		chain15.Diagnostics().chainStageRequests)
	{
		assert(requests == 1);
	}
	assert(chain15.Diagnostics().duplicateMilestonesSuppressed == 0);

	const std::filesystem::path originalWorkingDirectory =
		std::filesystem::current_path();
	const std::filesystem::path fixture =
		std::filesystem::temp_directory_path() /
		"caromino_combat_audio_fixture";
	const std::filesystem::path combatAssets =
		fixture / "assets" / "audio" / "combat";
	std::filesystem::create_directories(combatAssets);
	for (const char* filename : {
		"direct_impact.wav", "enemy_impact.wav", "enemy_defeat.wav",
		"chain_milestone.wav", "wall_impact.wav" })
	{
		WriteTestWav(combatAssets / filename);
	}
	std::filesystem::current_path(fixture);
	{
		AudioFeedback wavBackend;
		const bool deviceReady = wavBackend.Initialize(false);
		for (std::size_t index = 0;
			index < static_cast<std::size_t>(CombatSeCategory::Count);
			++index)
		{
			assert(wavBackend.HasLoadedAsset(
				static_cast<CombatSeCategory>(index)));
		}
		wavBackend.Update(1.0 / 60.0, 0.8f);
		wavBackend.OnCollisionImpact(directImpact, 0);
		wavBackend.OnShotFired(ShotFiredEvent{
			"player_heavy", static_cast<int>(BallCategory::Heavy) });
		for (int collision = 0; collision < 3; ++collision)
		{
			wavBackend.OnCollisionImpact(enemyImpact, collision + 1);
		}
		const AudioFeedbackDiagnostics& wavDiagnostics =
			wavBackend.Diagnostics();
		assert(wavDiagnostics.requests == 5);
		assert(wavDiagnostics.loadedAssetFiles == 5);
		assert(wavDiagnostics.missingAssetFiles == 4);
		assert(wavDiagnostics.chainStageRequests[0] == 1);
		if (deviceReady)
		{
			assert(wavDiagnostics.deviceInitialized);
			assert(wavDiagnostics.chainStagePlayed[0] == 1);
		}
		else
		{
			assert(wavDiagnostics.deviceUnavailableSkipped == 5);
		}
	}
	std::filesystem::current_path(originalWorkingDirectory);

	std::cout << "combat audio feedback tests passed\n";
	return 0;
}

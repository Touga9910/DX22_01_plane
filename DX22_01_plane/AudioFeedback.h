#pragma once

#include "CombatAudioRules.h"

#include <xaudio2.h>

#include <array>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

struct CollisionImpactEvent;
struct EnemyDamageEvent;
struct ShotFiredEvent;
struct WallImpactEvent;

struct AudioFeedbackDiagnostics final
{
	bool deviceInitialized = false;
	bool enabled = false;
	bool testToneMode = false;
	int activeVoices = 0;
	int maximumActiveVoices = 0;
	int voiceLimit = CombatAudioRules::MaximumVoices;
	std::uint64_t requests = 0;
	std::uint64_t played = 0;
	std::uint64_t cooldownSkipped = 0;
	std::uint64_t categoryLimitSkipped = 0;
	std::uint64_t voiceLimitSkipped = 0;
	std::uint64_t mutedSkipped = 0;
	std::uint64_t missingAssetSkipped = 0;
	std::uint64_t deviceUnavailableSkipped = 0;
	std::uint64_t backendErrorSkipped = 0;
	std::uint64_t loadedAudioBytes = 0;
	int loadedAssetFiles = 0;
	int missingAssetFiles = 0;
	std::uint64_t duplicateDefeatsSuppressed = 0;
	std::uint64_t duplicateMilestonesSuppressed = 0;
	std::array<std::uint64_t, 4> chainStageRequests{};
	std::array<std::uint64_t, 4> chainStagePlayed{};
	std::array<std::uint64_t,
		static_cast<std::size_t>(CombatSeCategory::Count)> categoryRequests{};
	std::array<std::uint64_t,
		static_cast<std::size_t>(CombatSeCategory::Count)> categoryPlayed{};
	double lastUpdateMilliseconds = 0.0;
	double totalUpdateMilliseconds = 0.0;
	double maximumUpdateMilliseconds = 0.0;
	std::uint64_t updateCount = 0;
	double totalRequestMilliseconds = 0.0;
	double maximumRequestMilliseconds = 0.0;
};

// A small battle-presentation audio component. It owns one XAudio2 graph and
// a bounded set of reusable SourceVoices; gameplay code only submits facts.
class AudioFeedback final
{
public:
	AudioFeedback() = default;
	~AudioFeedback();

	AudioFeedback(const AudioFeedback&) = delete;
	AudioFeedback& operator=(const AudioFeedback&) = delete;

	bool Initialize(bool allowGeneratedTestTones = false);
	void Shutdown();
	void Update(double elapsedSeconds, float seVolume);

	void OnShotFired(const ShotFiredEvent& event);
	void OnCollisionImpact(
		const CollisionImpactEvent& event,
		int existingHeavyChainCount);
	void OnEnemyDamage(const EnemyDamageEvent& event);
	void OnWallImpact(const WallImpactEvent& event);

	const AudioFeedbackDiagnostics& Diagnostics() const noexcept
	{
		return m_Diagnostics;
	}

	bool HasLoadedAsset(CombatSeCategory category) const noexcept;
	const std::string& AssetPath(CombatSeCategory category) const noexcept;

private:
	struct Clip final
	{
		std::string path;
		WAVEFORMATEX format{};
		std::vector<std::uint8_t> samples;
		bool loaded = false;
		bool generatedTestTone = false;
	};

	struct VoiceSlot final
	{
		IXAudio2SourceVoice* voice = nullptr;
		CombatSeCategory category = CombatSeCategory::DirectImpact;
		int priority = 0;
		bool active = false;
	};

	struct PlayRequest final
	{
		CombatSeCategory category = CombatSeCategory::DirectImpact;
		CollisionPresentationRules::Strength strength =
			CollisionPresentationRules::Strength::Weak;
		float speed = 0.0f;
		int chainMilestone = 0;
		bool preserveImportantEvent = false;
		int outcomeCount = 0;
	};

	bool LoadClip(CombatSeCategory category, const std::string& path);
	bool LoadClipFile(Clip& clip, const std::string& path);
	Clip& SelectClip(const PlayRequest& request);
	void GenerateTestTone(CombatSeCategory category);
	bool CreateVoices();
	void RefreshVoiceStates();
	void Request(const PlayRequest& request);
	VoiceSlot* FindAvailableVoice(CombatSeCategory category);
	bool IsCooldownActive(
		CombatSeCategory category,
		int priority,
		bool preserveImportantEvent) const;
	void RecordSkip(CombatSeSkipReason reason);
	void StopVoice(VoiceSlot& slot);

	IXAudio2* m_XAudio2 = nullptr;
	IXAudio2MasteringVoice* m_MasteringVoice = nullptr;
	std::array<Clip, static_cast<std::size_t>(CombatSeCategory::Count)> m_Clips{};
	std::array<Clip, 4> m_ChainStageClips{};
	std::vector<VoiceSlot> m_Voices;
	std::array<double, static_cast<std::size_t>(CombatSeCategory::Count)>
		m_LastPlayedSeconds{};
	std::array<int, static_cast<std::size_t>(CombatSeCategory::Count)>
		m_LastPlayedPriority{};
	std::unordered_set<std::uintptr_t> m_DefeatedTargets;
	std::array<bool, 4> m_TriggeredMilestones{};
	AudioFeedbackDiagnostics m_Diagnostics{};
	double m_NowSeconds = 0.0;
	float m_MasterVolume = 0.0f;
	int m_DefeatsThisShot = 0;
	bool m_Initialized = false;
};

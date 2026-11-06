#include "AudioFeedback.h"

#include "GameEvent.h"
#include "PlayerBallData.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>

#pragma comment(lib, "xaudio2.lib")

namespace
{
	constexpr std::array<const char*,
		static_cast<std::size_t>(CombatSeCategory::Count)> AssetPaths = {{
		"assets/audio/combat/direct_impact.wav",
		"assets/audio/combat/enemy_impact.wav",
		"assets/audio/combat/enemy_defeat.wav",
		"assets/audio/combat/chain_milestone.wav",
		"assets/audio/combat/wall_impact.wav",
	}};
	constexpr std::array<const char*, 4> ChainStageAssetPaths = {{
		"assets/audio/combat/chain_3.wav",
		"assets/audio/combat/chain_6.wav",
		"assets/audio/combat/chain_10.wav",
		"assets/audio/combat/chain_15.wav",
	}};

	bool FourCcEquals(const char* value, const char* expected)
	{
		return std::memcmp(value, expected, 4) == 0;
	}

	std::uint16_t ReadU16(const std::uint8_t* bytes)
	{
		return static_cast<std::uint16_t>(bytes[0]) |
			(static_cast<std::uint16_t>(bytes[1]) << 8);
	}

	std::uint32_t ReadU32(const std::uint8_t* bytes)
	{
		return static_cast<std::uint32_t>(bytes[0]) |
			(static_cast<std::uint32_t>(bytes[1]) << 8) |
			(static_cast<std::uint32_t>(bytes[2]) << 16) |
			(static_cast<std::uint32_t>(bytes[3]) << 24);
	}

	int MilestoneIndex(int milestone)
	{
		for (int index = 0; index < 4; ++index)
		{
			if (CollisionPresentationRules::ChainMilestones[index] == milestone)
				return index;
		}
		return -1;
	}
}

AudioFeedback::~AudioFeedback()
{
	Shutdown();
}

bool AudioFeedback::Initialize(bool allowGeneratedTestTones)
{
	Shutdown();
	m_Diagnostics = {};
	m_Diagnostics.voiceLimit = CombatAudioRules::MaximumVoices;
	m_LastPlayedSeconds.fill(-1000.0);
	m_LastPlayedPriority.fill(std::numeric_limits<int>::min());
	m_DefeatedTargets.clear();
	m_TriggeredMilestones.fill(false);
	m_NowSeconds = 0.0;
	m_MasterVolume = 0.0f;
	m_DefeatsThisShot = 0;

	for (std::size_t index = 0; index < m_Clips.size(); ++index)
	{
		const CombatSeCategory category =
			static_cast<CombatSeCategory>(index);
		if (!LoadClip(category, AssetPaths[index]) && allowGeneratedTestTones)
		{
			GenerateTestTone(category);
			m_Diagnostics.testToneMode = true;
		}
	}
	for (std::size_t index = 0; index < m_ChainStageClips.size(); ++index)
	{
		LoadClipFile(m_ChainStageClips[index], ChainStageAssetPaths[index]);
	}

	HRESULT result = XAudio2Create(&m_XAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);
	if (FAILED(result) || m_XAudio2 == nullptr)
	{
		m_XAudio2 = nullptr;
		return false;
	}
	result = m_XAudio2->CreateMasteringVoice(&m_MasteringVoice);
	if (FAILED(result) || m_MasteringVoice == nullptr)
	{
		m_XAudio2->Release();
		m_XAudio2 = nullptr;
		return false;
	}

	m_Diagnostics.deviceInitialized = true;
	m_Initialized = CreateVoices();
	return m_Initialized;
}

void AudioFeedback::Shutdown()
{
	for (VoiceSlot& slot : m_Voices)
	{
		if (slot.voice != nullptr)
		{
			StopVoice(slot);
			slot.voice->DestroyVoice();
			slot.voice = nullptr;
		}
	}
	m_Voices.clear();
	if (m_MasteringVoice != nullptr)
	{
		m_MasteringVoice->DestroyVoice();
		m_MasteringVoice = nullptr;
	}
	if (m_XAudio2 != nullptr)
	{
		m_XAudio2->Release();
		m_XAudio2 = nullptr;
	}
	m_Initialized = false;
	m_Diagnostics.deviceInitialized = false;
	m_Diagnostics.enabled = false;
	m_Diagnostics.activeVoices = 0;
}

void AudioFeedback::Update(double elapsedSeconds, float seVolume)
{
	const auto started = std::chrono::steady_clock::now();
	if (std::isfinite(elapsedSeconds) && elapsedSeconds > 0.0)
	{
		m_NowSeconds += (std::min)(elapsedSeconds, 0.25);
	}
	m_MasterVolume = std::isfinite(seVolume)
		? std::clamp(seVolume, 0.0f, 1.0f)
		: 0.0f;
	if (m_MasteringVoice != nullptr)
	{
		if (FAILED(m_MasteringVoice->SetVolume(m_MasterVolume)))
		{
			++m_Diagnostics.backendErrorSkipped;
		}
	}
	RefreshVoiceStates();
	m_Diagnostics.enabled = m_Diagnostics.deviceInitialized &&
		m_MasterVolume > 0.0f;
	const double milliseconds = std::chrono::duration<double, std::milli>(
		std::chrono::steady_clock::now() - started).count();
	m_Diagnostics.lastUpdateMilliseconds = milliseconds;
	m_Diagnostics.totalUpdateMilliseconds += milliseconds;
	m_Diagnostics.maximumUpdateMilliseconds = (std::max)(
		m_Diagnostics.maximumUpdateMilliseconds,
		milliseconds);
	++m_Diagnostics.updateCount;
}

void AudioFeedback::OnShotFired(const ShotFiredEvent&)
{
	m_DefeatedTargets.clear();
	m_TriggeredMilestones.fill(false);
	m_DefeatsThisShot = 0;
}

void AudioFeedback::OnCollisionImpact(
	const CollisionImpactEvent& event,
	int existingHeavyChainCount)
{
	const CollisionPresentationRules::Strength strength =
		CollisionPresentationRules::Classify(event.relativeApproachSpeed);
	if (event.collisionKind == CombatCollisionKind::PlayerEnemy)
	{
		Request({ CombatSeCategory::DirectImpact, strength,
			event.relativeApproachSpeed, 0, false });
		return;
	}
	if (event.collisionKind != CombatCollisionKind::EnemyEnemy)
	{
		return;
	}

	Request({ CombatSeCategory::EnemyImpact, strength,
		event.relativeApproachSpeed, 0, false });
	if (event.shotBallCategory != static_cast<int>(BallCategory::Heavy))
	{
		return;
	}

	if (!CollisionPresentationRules::IsChainMilestone(existingHeavyChainCount))
	{
		return;
	}
	const int milestoneIndex = MilestoneIndex(existingHeavyChainCount);
	if (milestoneIndex < 0 || m_TriggeredMilestones[milestoneIndex])
	{
		++m_Diagnostics.duplicateMilestonesSuppressed;
		return;
	}
	m_TriggeredMilestones[milestoneIndex] = true;
	Request({ CombatSeCategory::ChainMilestone, strength,
		event.relativeApproachSpeed, existingHeavyChainCount, true });
}

void AudioFeedback::OnEnemyDamage(const EnemyDamageEvent& event)
{
	if (!event.defeated)
	{
		return;
	}
	if (event.targetKey != 0 &&
		!m_DefeatedTargets.insert(event.targetKey).second)
	{
		++m_Diagnostics.duplicateDefeatsSuppressed;
		return;
	}
	++m_DefeatsThisShot;
	Request({ CombatSeCategory::EnemyDefeat,
		CollisionPresentationRules::Strength::Strong, 0.0f, 0, false,
		m_DefeatsThisShot });
}

void AudioFeedback::OnWallImpact(const WallImpactEvent& event)
{
	Request({ CombatSeCategory::WallImpact,
		CollisionPresentationRules::Classify(event.relativeApproachSpeed),
		event.relativeApproachSpeed, 0, false });
}

bool AudioFeedback::HasLoadedAsset(CombatSeCategory category) const noexcept
{
	const std::size_t index = CombatAudioRules::Index(category);
	return index < m_Clips.size() && m_Clips[index].loaded;
}

const std::string& AudioFeedback::AssetPath(
	CombatSeCategory category) const noexcept
{
	static const std::string Empty;
	const std::size_t index = CombatAudioRules::Index(category);
	return index < m_Clips.size() ? m_Clips[index].path : Empty;
}

bool AudioFeedback::LoadClip(
	CombatSeCategory category,
	const std::string& path)
{
	Clip& clip = m_Clips[CombatAudioRules::Index(category)];
	return LoadClipFile(clip, path);
}

bool AudioFeedback::LoadClipFile(Clip& clip, const std::string& path)
{
	clip = {};
	clip.path = path;
	std::ifstream stream(path, std::ios::binary);
	if (!stream)
	{
		++m_Diagnostics.missingAssetFiles;
		return false;
	}
	stream.seekg(0, std::ios::end);
	const std::streamoff fileSize = stream.tellg();
	stream.seekg(0, std::ios::beg);
	if (fileSize < 44 || fileSize > 16 * 1024 * 1024)
	{
		++m_Diagnostics.missingAssetFiles;
		return false;
	}
	std::vector<std::uint8_t> file(static_cast<std::size_t>(fileSize));
	stream.read(reinterpret_cast<char*>(file.data()), fileSize);
	if (!stream || !FourCcEquals(reinterpret_cast<const char*>(file.data()), "RIFF") ||
		!FourCcEquals(reinterpret_cast<const char*>(file.data() + 8), "WAVE"))
	{
		++m_Diagnostics.missingAssetFiles;
		return false;
	}

	bool foundFormat = false;
	bool foundData = false;
	std::size_t offset = 12;
	while (offset + 8 <= file.size())
	{
		const char* chunkId = reinterpret_cast<const char*>(file.data() + offset);
		const std::uint32_t chunkSize = ReadU32(file.data() + offset + 4);
		offset += 8;
		if (chunkSize > file.size() - offset)
		{
			++m_Diagnostics.missingAssetFiles;
			return false;
		}
		if (FourCcEquals(chunkId, "fmt "))
		{
			if (chunkSize < 16)
			{
				++m_Diagnostics.missingAssetFiles;
				return false;
			}
			const std::uint8_t* format = file.data() + offset;
			clip.format.wFormatTag = ReadU16(format + 0);
			clip.format.nChannels = ReadU16(format + 2);
			clip.format.nSamplesPerSec = ReadU32(format + 4);
			clip.format.nAvgBytesPerSec = ReadU32(format + 8);
			clip.format.nBlockAlign = ReadU16(format + 12);
			clip.format.wBitsPerSample = ReadU16(format + 14);
			clip.format.cbSize = 0;
			foundFormat = true;
		}
		else if (FourCcEquals(chunkId, "data"))
		{
			clip.samples.assign(
				file.begin() + static_cast<std::ptrdiff_t>(offset),
				file.begin() + static_cast<std::ptrdiff_t>(offset + chunkSize));
			foundData = !clip.samples.empty();
		}
		offset += chunkSize + (chunkSize & 1u);
	}

	const bool formatSupported = foundFormat &&
		clip.format.wFormatTag == WAVE_FORMAT_PCM &&
		(clip.format.nChannels == 1 || clip.format.nChannels == 2) &&
		clip.format.wBitsPerSample == 16 &&
		(clip.format.nSamplesPerSec == 44100 ||
			clip.format.nSamplesPerSec == 48000) &&
		clip.format.nBlockAlign ==
			clip.format.nChannels * clip.format.wBitsPerSample / 8 &&
		clip.format.nAvgBytesPerSec ==
			clip.format.nSamplesPerSec * clip.format.nBlockAlign;
	clip.loaded = formatSupported && foundData &&
		clip.samples.size() % clip.format.nBlockAlign == 0;
	if (!clip.loaded)
	{
		clip.samples.clear();
		++m_Diagnostics.missingAssetFiles;
	}
	else
	{
		++m_Diagnostics.loadedAssetFiles;
		m_Diagnostics.loadedAudioBytes += clip.samples.size();
	}
	return clip.loaded;
}

AudioFeedback::Clip& AudioFeedback::SelectClip(const PlayRequest& request)
{
	Clip& fallback = m_Clips[CombatAudioRules::Index(request.category)];
	if (request.category != CombatSeCategory::ChainMilestone)
	{
		return fallback;
	}
	const int index = MilestoneIndex(request.chainMilestone);
	if (index < 0 || !m_ChainStageClips[index].loaded)
	{
		return fallback;
	}
	const Clip& candidate = m_ChainStageClips[index];
	const bool sameFormat = candidate.format.wFormatTag == fallback.format.wFormatTag &&
		candidate.format.nChannels == fallback.format.nChannels &&
		candidate.format.nSamplesPerSec == fallback.format.nSamplesPerSec &&
		candidate.format.wBitsPerSample == fallback.format.wBitsPerSample &&
		candidate.format.nBlockAlign == fallback.format.nBlockAlign;
	return sameFormat ? m_ChainStageClips[index] : fallback;
}

void AudioFeedback::GenerateTestTone(CombatSeCategory category)
{
	Clip& clip = m_Clips[CombatAudioRules::Index(category)];
	clip.format = {};
	clip.format.wFormatTag = WAVE_FORMAT_PCM;
	clip.format.nChannels = 1;
	clip.format.nSamplesPerSec = 48000;
	clip.format.wBitsPerSample = 16;
	clip.format.nBlockAlign = 2;
	clip.format.nAvgBytesPerSec =
		clip.format.nSamplesPerSec * clip.format.nBlockAlign;

	const std::size_t index = CombatAudioRules::Index(category);
	const std::array<float, 5> frequencies = { 150.0f, 260.0f, 520.0f,
		690.0f, 210.0f };
	const std::array<float, 5> durations = { 0.11f, 0.07f, 0.16f,
		0.19f, 0.065f };
	const int sampleCount = static_cast<int>(
		durations[index] * static_cast<float>(clip.format.nSamplesPerSec));
	clip.samples.resize(static_cast<std::size_t>(sampleCount) * 2);
	for (int sample = 0; sample < sampleCount; ++sample)
	{
		const float time = static_cast<float>(sample) /
			static_cast<float>(clip.format.nSamplesPerSec);
		const float normalized = static_cast<float>(sample) /
			static_cast<float>((std::max)(1, sampleCount - 1));
		const float attack = (std::min)(1.0f, normalized * 18.0f);
		const float envelope = attack * (1.0f - normalized) *
			(1.0f - normalized);
		const float overtone = std::sin(
			2.0f * 3.14159265358979323846f * frequencies[index] *
			2.01f * time) * 0.28f;
		const float wave = (std::sin(
			2.0f * 3.14159265358979323846f * frequencies[index] * time) +
			overtone) * envelope * 0.42f;
		const std::int16_t value = static_cast<std::int16_t>(
			std::clamp(wave, -1.0f, 1.0f) * 32767.0f);
		clip.samples[static_cast<std::size_t>(sample) * 2] =
			static_cast<std::uint8_t>(value & 0xff);
		clip.samples[static_cast<std::size_t>(sample) * 2 + 1] =
			static_cast<std::uint8_t>((value >> 8) & 0xff);
	}
	clip.loaded = true;
	clip.generatedTestTone = true;
}

bool AudioFeedback::CreateVoices()
{
	if (m_XAudio2 == nullptr)
	{
		return false;
	}
	for (std::size_t index = 0; index < m_Clips.size(); ++index)
	{
		Clip& clip = m_Clips[index];
		if (!clip.loaded)
		{
			continue;
		}
		const CombatSeCategory category =
			static_cast<CombatSeCategory>(index);
		const int voiceCount = CombatAudioRules::Get(category).maximumVoices;
		for (int voiceIndex = 0; voiceIndex < voiceCount; ++voiceIndex)
		{
			VoiceSlot slot{};
			slot.category = category;
			if (FAILED(m_XAudio2->CreateSourceVoice(
				&slot.voice,
				&clip.format,
				0,
				CombatAudioRules::MaximumPitch)))
			{
				++m_Diagnostics.backendErrorSkipped;
				continue;
			}
			m_Voices.push_back(slot);
		}
	}
	return true;
}

void AudioFeedback::RefreshVoiceStates()
{
	int activeVoices = 0;
	for (VoiceSlot& slot : m_Voices)
	{
		if (slot.voice == nullptr || !slot.active)
		{
			continue;
		}
		XAUDIO2_VOICE_STATE state{};
		slot.voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
		if (state.BuffersQueued == 0)
		{
			slot.active = false;
			slot.priority = 0;
		}
		else
		{
			++activeVoices;
		}
	}
	m_Diagnostics.activeVoices = activeVoices;
	m_Diagnostics.maximumActiveVoices = (std::max)(
		m_Diagnostics.maximumActiveVoices,
		activeVoices);
}

void AudioFeedback::Request(const PlayRequest& request)
{
	const auto started = std::chrono::steady_clock::now();
	const std::size_t index = CombatAudioRules::Index(request.category);
	++m_Diagnostics.requests;
	++m_Diagnostics.categoryRequests[index];
	const int chainStageIndex = request.category == CombatSeCategory::ChainMilestone
		? MilestoneIndex(request.chainMilestone)
		: -1;
	if (chainStageIndex >= 0)
	{
		++m_Diagnostics.chainStageRequests[chainStageIndex];
	}
	const int priority = CombatAudioRules::Priority(
		request.category,
		request.strength);
	CombatSeSkipReason skipReason = CombatSeSkipReason::None;
	if (m_MasterVolume <= 0.0f)
	{
		skipReason = CombatSeSkipReason::Muted;
	}
	else if (!m_Diagnostics.deviceInitialized || !m_Initialized)
	{
		skipReason = CombatSeSkipReason::DeviceUnavailable;
	}
	else if (!m_Clips[index].loaded)
	{
		skipReason = CombatSeSkipReason::MissingAsset;
	}
	else if (IsCooldownActive(
		request.category,
		priority,
		request.preserveImportantEvent))
	{
		skipReason = CombatSeSkipReason::Cooldown;
	}

	VoiceSlot* slot = nullptr;
	if (skipReason == CombatSeSkipReason::None)
	{
		slot = FindAvailableVoice(request.category);
		if (slot == nullptr)
		{
			RefreshVoiceStates();
			slot = FindAvailableVoice(request.category);
		}
		if (slot == nullptr)
		{
			skipReason = m_Diagnostics.activeVoices >=
				CombatAudioRules::MaximumVoices
				? CombatSeSkipReason::VoiceLimit
				: CombatSeSkipReason::CategoryLimit;
		}
	}

	if (skipReason == CombatSeSkipReason::None && slot != nullptr)
	{
		Clip& clip = SelectClip(request);
		XAUDIO2_BUFFER buffer{};
		buffer.AudioBytes = static_cast<UINT32>(clip.samples.size());
		buffer.pAudioData = clip.samples.data();
		buffer.Flags = XAUDIO2_END_OF_STREAM;
		float volume = CombatAudioRules::Volume(
			request.category,
			request.strength,
			request.speed,
			request.chainMilestone);
		float pitch = CombatAudioRules::Pitch(
			request.category,
			request.strength,
			request.chainMilestone);
		if (request.category == CombatSeCategory::EnemyDefeat &&
			request.outcomeCount > 1)
		{
			const float countBoost = static_cast<float>((std::min)(
				request.outcomeCount - 1, 2));
			volume = (std::min)(
				CombatAudioRules::Get(request.category).maximumVolume,
				volume * (1.0f + 0.06f * countBoost));
			pitch = std::clamp(
				pitch + 0.01f * countBoost,
				CombatAudioRules::MinimumPitch,
				CombatAudioRules::MaximumPitch);
		}
		HRESULT result = slot->voice->SetVolume(volume);
		if (SUCCEEDED(result)) result = slot->voice->SetFrequencyRatio(pitch);
		if (SUCCEEDED(result)) result = slot->voice->SubmitSourceBuffer(&buffer);
		if (SUCCEEDED(result)) result = slot->voice->Start(0);
		if (FAILED(result))
		{
			slot->voice->FlushSourceBuffers();
			skipReason = CombatSeSkipReason::BackendError;
		}
		else
		{
			slot->active = true;
			slot->priority = priority;
			m_LastPlayedSeconds[index] = m_NowSeconds;
			m_LastPlayedPriority[index] = priority;
			++m_Diagnostics.played;
			++m_Diagnostics.categoryPlayed[index];
			if (chainStageIndex >= 0)
			{
				++m_Diagnostics.chainStagePlayed[chainStageIndex];
			}
			RefreshVoiceStates();
		}
	}
	if (skipReason != CombatSeSkipReason::None)
	{
		RecordSkip(skipReason);
	}
	const double milliseconds = std::chrono::duration<double, std::milli>(
		std::chrono::steady_clock::now() - started).count();
	m_Diagnostics.totalRequestMilliseconds += milliseconds;
	m_Diagnostics.maximumRequestMilliseconds = (std::max)(
		m_Diagnostics.maximumRequestMilliseconds,
		milliseconds);
}

AudioFeedback::VoiceSlot* AudioFeedback::FindAvailableVoice(
	CombatSeCategory category)
{
	for (VoiceSlot& slot : m_Voices)
	{
		if (slot.category != category || slot.voice == nullptr)
		{
			continue;
		}
		if (slot.active)
		{
			XAUDIO2_VOICE_STATE state{};
			slot.voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
			if (state.BuffersQueued == 0)
			{
				slot.active = false;
				slot.priority = 0;
			}
		}
		if (!slot.active)
		{
			return &slot;
		}
	}
	return nullptr;
}

bool AudioFeedback::IsCooldownActive(
	CombatSeCategory category,
	int priority,
	bool preserveImportantEvent) const
{
	if (preserveImportantEvent)
	{
		return false;
	}
	const std::size_t index = CombatAudioRules::Index(category);
	const double elapsed = m_NowSeconds - m_LastPlayedSeconds[index];
	return elapsed < CombatAudioRules::Get(category).cooldownSeconds &&
		priority <= m_LastPlayedPriority[index];
}

void AudioFeedback::RecordSkip(CombatSeSkipReason reason)
{
	switch (reason)
	{
	case CombatSeSkipReason::Muted:
		++m_Diagnostics.mutedSkipped;
		break;
	case CombatSeSkipReason::DeviceUnavailable:
		++m_Diagnostics.deviceUnavailableSkipped;
		break;
	case CombatSeSkipReason::MissingAsset:
		++m_Diagnostics.missingAssetSkipped;
		break;
	case CombatSeSkipReason::Cooldown:
		++m_Diagnostics.cooldownSkipped;
		break;
	case CombatSeSkipReason::CategoryLimit:
		++m_Diagnostics.categoryLimitSkipped;
		break;
	case CombatSeSkipReason::VoiceLimit:
		++m_Diagnostics.voiceLimitSkipped;
		break;
	case CombatSeSkipReason::BackendError:
		++m_Diagnostics.backendErrorSkipped;
		break;
	default:
		break;
	}
}

void AudioFeedback::StopVoice(VoiceSlot& slot)
{
	if (slot.voice == nullptr)
	{
		return;
	}
	slot.voice->Stop(0);
	slot.voice->FlushSourceBuffers();
	slot.active = false;
	slot.priority = 0;
}

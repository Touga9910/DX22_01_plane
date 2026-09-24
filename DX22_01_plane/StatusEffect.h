#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

// 攻撃力・防御力へ加算する状態効果の種類を表す
// 表示やボール種別には依存せず、敵ごとの状態効果などから共通利用
enum class StatusEffectType
{
	AttackUp,		// 攻撃力を増加
	AttackDown,		// 攻撃力を減少
	DefenseUp,		// 防御力を増加
	DefenseDown,	// 防御力を減少
	Count,			// StatusEffectTypeの要素数
};

// 実際に処理対象とする状態効果種別の一覧
inline constexpr std::array<StatusEffectType, 4> AllStatusEffectTypes = {
	StatusEffectType::AttackUp,
	StatusEffectType::AttackDown,
	StatusEffectType::DefenseUp,
	StatusEffectType::DefenseDown,
};

// StatusEffectTypeを保存・JSON用の文字列へ変換
// 未定義の値が渡された場合は"unknown"を返す
inline constexpr const char* ToString(StatusEffectType type)
{
	switch (type)
	{
	case StatusEffectType::AttackUp: return "attack_up";
	case StatusEffectType::AttackDown: return "attack_down";
	case StatusEffectType::DefenseUp: return "defense_up";
	case StatusEffectType::DefenseDown: return "defense_down";
	default: return "unknown";
	}
}

// 文字列からStatusEffectTypeを復元
// 対応する文字列を取得できた場合はresultへ設定してtrueを返し、未対応の場合はfalseを返す
inline bool TryParseStatusEffectType(std::string_view value, StatusEffectType& result)
{
	for (const StatusEffectType type : AllStatusEffectTypes)
	{
		if (value == ToString(type))
		{
			result = type;
			return true;
		}
	}
	return false;
}

// 攻撃力へ影響する状態効果の場合はtrueを返す
inline constexpr bool IsAttackStatusEffect(StatusEffectType type)
{
	return type == StatusEffectType::AttackUp || type == StatusEffectType::AttackDown;
}

// 能力値を上昇させる状態効果の場合はtrueを返す
inline constexpr bool IsPositiveStatusEffect(StatusEffectType type)
{
	return type == StatusEffectType::AttackUp || type == StatusEffectType::DefenseUp;
}

// 状態効果種別ごとの効果量をまとめて保持
// 効果量0は、その状態効果が付与されていないことを表す
class StatusEffectCollection
{
public:
	static constexpr int MaxMagnitude = 999;	// 1種類の状態効果が保持できる最大効果量

	// -------------------------
	// 状態効果の変更
	// -------------------------

	// 指定した状態効果の効果量を0～MaxMagnitudeの範囲で設定
	// typeが配列範囲外の場合は何も変更しない
	void Set(StatusEffectType type, int magnitude)
	{
		const std::size_t index = static_cast<std::size_t>(type);
		if (index >= m_Magnitudes.size()) return;
		m_Magnitudes[index] = std::clamp(magnitude, 0, MaxMagnitude);
	}

	void Remove(StatusEffectType type) { Set(type, 0); }	// 指定した状態効果を解除
	void Clear() { m_Magnitudes.fill(0); }					// すべての状態効果を解除

	// -------------------------
	// 状態効果の取得
	// -------------------------

	// 指定した状態効果の効果量を返す
	// typeが配列範囲外の場合は0を返す
	int GetMagnitude(StatusEffectType type) const
	{
		const std::size_t index = static_cast<std::size_t>(type);
		return index < m_Magnitudes.size() ? m_Magnitudes[index] : 0;
	}

	bool Has(StatusEffectType type) const { return GetMagnitude(type) > 0; }	// 指定した状態効果が有効ならtrueを返す

	// AttackUpからAttackDownを差し引いた攻撃力補正値を返す
	int GetAttackModifier() const
	{
		return GetMagnitude(StatusEffectType::AttackUp) -
			GetMagnitude(StatusEffectType::AttackDown);
	}

	// DefenseUpからDefenseDownを差し引いた防御力補正値を返す
	int GetDefenseModifier() const
	{
		return GetMagnitude(StatusEffectType::DefenseUp) -
			GetMagnitude(StatusEffectType::DefenseDown);
	}

	// 効果量が1以上の状態効果が何種類あるか返す
	int GetActiveCount() const
	{
		return static_cast<int>(std::count_if(
			m_Magnitudes.begin(), m_Magnitudes.end(),
			[](int magnitude) { return magnitude > 0; }));
	}

	bool Empty() const { return GetActiveCount() == 0; }	// 有効な状態効果が1つもなければtrueを返す

private:
	// -------------------------
	// メンバー変数
	// -------------------------

	std::array<int, static_cast<std::size_t>(StatusEffectType::Count)> m_Magnitudes{};	// 状態効果種別ごとの現在効果量
};

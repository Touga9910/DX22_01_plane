#pragma once

// プレイヤー向けUI文字列をUTF-8としてここへ集約する。
// Visual StudioがCP932を使用する場合でも安全に扱えるよう、Unicodeエスケープを用いる。
namespace UiText
{
	inline const char* Utf8(const char8_t* text) noexcept
	{
		return reinterpret_cast<const char*>(text);
	}

	inline const char* const TitleWindow = Utf8(u8"\u30e9\u30f3\u30e1\u30cb\u30e5\u30fc");
	inline const char* const NewRun = Utf8(u8"\u65b0\u3057\u3044\u30e9\u30f3");
	inline const char* const ContinueRun = Utf8(u8"\u7d9a\u304d\u304b\u3089");
	inline const char* const OverwriteConfirm = Utf8(u8"\u65e2\u5b58\u306e\u30bb\u30fc\u30d6\u304c\u3042\u308a\u307e\u3059\u3002\u300c\u4e0a\u66f8\u304d\u3057\u3066\u65b0\u3057\u304f\u59cb\u3081\u308b\u300d\u3067\u958b\u59cb\u3057\u307e\u3059\u3002");
	inline const char* const TitleControls = Utf8(u8"W/S\u30fb\u4e0a\u4e0b\u30ad\u30fc\uff1a\u9078\u629e\u3000ENTER\uff1a\u6c7a\u5b9a");
	inline const char* const ManualSaveHint = Utf8(u8"\u4e0a\u90e8\u306e\u300c\u30bb\u30fc\u30d6\u300d\u304b\u3089\u4fdd\u5b58\u3067\u304d\u307e\u3059\uff08F6\u3082\u4f7f\u7528\u53ef\u80fd\uff09\u3002");

	inline const char* const RouteWindow = Utf8(u8"\u30eb\u30fc\u30c8\u9078\u629e");
	inline const char* const FloorFormat = Utf8(u8"\u30d5\u30ed\u30a2 %d");
	inline const char* const RunStatusFormat = Utf8(u8"HP %d / %d\u3000\u6240\u6301\u91d1 %d\u3000\u30c7\u30c3\u30ad %d");
	inline const char* const ChooseNode = Utf8(u8"\u6b21\u306e\u884c\u304d\u5148\u3092\u9078\u3093\u3067\u304f\u3060\u3055\u3044");
	inline const char* const RouteBattle = Utf8(u8"\u6226\u95d8");
	inline const char* const RouteShop = Utf8(u8"\u30b7\u30e7\u30c3\u30d7");
	inline const char* const RouteRest = Utf8(u8"\u4f11\u61a9\u6240");
	inline const char* const NextBattleRandom = Utf8(u8"\u6b21\u306e\u6226\u95d8\u30b9\u30c6\u30fc\u30b8\uff1a\u30e9\u30f3\u30c0\u30e0");
	inline const char* const RouteSelectControls = Utf8(u8"W/S\u30fb\u4e0a\u4e0b\u30ad\u30fc\uff1a\u9078\u629e");
	inline const char* const RouteEnterControls = Utf8(u8"ENTER\u30fbSPACE\uff1a\u79fb\u52d5");

	inline const char* const ShopWindow = Utf8(u8"\u30b7\u30e7\u30c3\u30d7");
	inline const char* const ShopStatusFormat = Utf8(u8"\u6240\u6301\u91d1 %d\u3000\u30c7\u30c3\u30ad %d\u3000\u30ec\u30ea\u30c3\u30af %d/%d");
	inline const char* const BuyBallFormat = Utf8(u8"%s \u30dc\u30fc\u30eb\u3092\u8cfc\u5165 - %d\u30de\u30cd\u30fc");
	inline const char* const RemoveBallFormat = Utf8(u8"%s \u30dc\u30fc\u30eb\u3092\u524a\u9664 - %d\u30de\u30cd\u30fc");
	inline const char* const BuyRelicFormat = Utf8(u8"%s \u30ec\u30ea\u30c3\u30af\u3092\u8cfc\u5165");
	inline const char* const LeaveShopFormat = Utf8(u8"%s \u5e97\u3092\u51fa\u308b");
	inline const char* const BallCatalog = Utf8(u8"\u30dc\u30fc\u30eb\u4e00\u89a7\uff08\u5de6\u53f3\u30ad\u30fc\u3067\u9078\u629e\uff09");
	inline const char* const BallStatsFormat = Utf8(u8"%s %s\u3000\u653b\u6483:%d \u9632\u5fa1:%d \u91cd\u3055:%.1f");
	inline const char* const BallCombatStatsFormat = Utf8(u8"%s [%d] %s\u3000\u653b\u6483:%d \u9632\u5fa1:%d");
	inline const char* const BallUpgradeStatsFormat = Utf8(u8"%s [%d] %s\u3000+%d\u3000\u653b\u6483:%d \u9632\u5fa1:%d");
	inline const char* const RewardBallStatsFormat = Utf8(u8"%s %s\u3000\u653b\u6483:%d \u9632\u5fa1:%d");
	inline const char* const BallPhysicsFormat = Utf8(u8"    \u53cd\u767a:%.2f\u3000\u6469\u64e6:%.2f\u3000\u5927\u304d\u3055:%.1f");
	inline const char* const BallTraitsFormat = Utf8(u8"    \u8cab\u901a:%s\u3000\u30a2\u30f3\u30ab\u30fc:%s");
	inline const char* const Yes = Utf8(u8"\u3042\u308a");
	inline const char* const No = Utf8(u8"\u306a\u3057");
	inline const char* const RemoveCatalog = Utf8(u8"\u524a\u9664\u3059\u308b\u30dc\u30fc\u30eb\uff08\u5de6\u53f3\u30ad\u30fc\u3067\u9078\u629e\uff09");
	inline const char* const MinimumDeckFormat = Utf8(u8"\u30c7\u30c3\u30ad\u306e\u6700\u4f4e\u679a\u6570\uff1a%d");
	inline const char* const RelicCatalog = Utf8(u8"\u30ec\u30ea\u30c3\u30af\u4e00\u89a7\uff08\u5de6\u53f3\u30ad\u30fc\u3067\u9078\u629e\uff09");
	inline const char* const RelicLineFormat = Utf8(u8"%s %s - %d\u30de\u30cd\u30fc%s");
	inline const char* const OwnedSuffix = Utf8(u8"\u3000[\u6240\u6301\u6e08\u307f]");
	inline const char* const ShopSelectControls = Utf8(u8"W/S\u30fb\u4e0a\u4e0b\u30ad\u30fc\uff1a\u9805\u76ee\u3092\u9078\u629e");
	inline const char* const ShopItemControls = Utf8(u8"A/D\u30fb\u5de6\u53f3\u30ad\u30fc\uff1a\u54c1\u7269\u3092\u9078\u629e\u3000ENTER\u30fbSPACE\uff1a\u6c7a\u5b9a");
	inline const char* const RelicPurchasedSuffix = Utf8(u8"\u3092\u8cfc\u5165\u3057\u307e\u3057\u305f\u3002");
	inline const char* const RelicAlreadyOwned = Utf8(u8"\u305d\u306e\u30ec\u30ea\u30c3\u30af\u306f\u3059\u3067\u306b\u6240\u6301\u3057\u3066\u3044\u307e\u3059\u3002");
	inline const char* const PurchaseFailed = Utf8(u8"\u8cfc\u5165\u3067\u304d\u307e\u305b\u3093\u3002\u6240\u6301\u91d1\u3092\u78ba\u8a8d\u3057\u3066\u304f\u3060\u3055\u3044\u3002");
	inline const char* const BallPurchased = Utf8(u8"\u30dc\u30fc\u30eb\u3092\u8cfc\u5165\u3057\u307e\u3057\u305f\u3002");
	inline const char* const BallRemoved = Utf8(u8"\u30dc\u30fc\u30eb\u3092\u30c7\u30c3\u30ad\u304b\u3089\u524a\u9664\u3057\u307e\u3057\u305f\u3002");
	inline const char* const RemovalFailed = Utf8(u8"\u524a\u9664\u3067\u304d\u307e\u305b\u3093\u3002\u6700\u4f4e5\u500b\u306e\u30dc\u30fc\u30eb\u3068\u6240\u6301\u91d1\u3092\u78ba\u8a8d\u3057\u3066\u304f\u3060\u3055\u3044\u3002");

	inline const char* const RestWindow = Utf8(u8"\u4f11\u61a9\u6240");
	inline const char* const ChooseRestAction = Utf8(u8"\u3053\u306e\u8a2a\u554f\u3067\u306f\u7121\u6599\u306e\u884c\u52d5\u30921\u3064\u9078\u3079\u307e\u3059\u3002");
	inline const char* const RestActionFormat = Utf8(u8"%s \u4f11\u61a9 - \u6700\u5927HP\u306e%d%%\u56de\u5fa9\uff08+%d\uff09");
	inline const char* const UpgradeActionFormat = Utf8(u8"%s \u5f37\u5316 - \u6b21\u306e\u56fa\u5b9a\u30ec\u30d9\u30eb\u3078\uff08\u6700\u5927+2\uff09");
	inline const char* const LeaveRestFormat = Utf8(u8"%s \u4f11\u61a9\u6240\u3092\u51fa\u308b");
	inline const char* const UpgradeTarget = Utf8(u8"\u5f37\u5316\u3059\u308b\u30dc\u30fc\u30eb\uff08\u5de6\u53f3\u30ad\u30fc\u3067\u9078\u629e\uff09");
	inline const char* const NextStatsFormat = Utf8(u8"    \u6b21\uff1a\u653b\u6483:%d \u9632\u5fa1:%d");
	inline const char* const MaxUpgrade = Utf8(u8"    \u6700\u5927\u5f37\u5316 +2");
	inline const char* const RestControls = Utf8(u8"W/S\u30fb\u4e0a\u4e0b\u30ad\u30fc\uff1a\u9078\u629e\u3000ENTER\u30fbSPACE\uff1a\u6c7a\u5b9a");
	inline const char* const MustChooseRest = Utf8(u8"\u4f11\u61a9\u6240\u3092\u51fa\u308b\u524d\u306b\u884c\u52d5\u30921\u3064\u9078\u3093\u3067\u304f\u3060\u3055\u3044\u3002");
	inline const char* const RestAlreadyUsed = Utf8(u8"\u3053\u306e\u4f11\u61a9\u6240\u3067\u306e\u884c\u52d5\u306f\u3059\u3067\u306b\u4f7f\u7528\u6e08\u307f\u3067\u3059\u3002");
	inline const char* const RecoveredFormat = Utf8(u8"\u6700\u5927HP\u306e%d%%\u5206\u3092\u56de\u5fa9\u3057\u307e\u3057\u305f\u3002");
	inline const char* const HpAlreadyFull = Utf8(u8"HP\u306f\u3059\u3067\u306b\u6700\u5927\u3067\u3059\u3002");
	inline const char* const BallUpgraded = Utf8(u8"\u30dc\u30fc\u30eb\u3092\u6b21\u306e\u56fa\u5b9a\u30ec\u30d9\u30eb\u3078\u5f37\u5316\u3057\u307e\u3057\u305f\u3002");
	inline const char* const BallAlreadyMax = Utf8(u8"\u3053\u306e\u30dc\u30fc\u30eb\u306f\u3059\u3067\u306b+2\u3067\u3059\u3002");

	inline const char* const ClearWindow = Utf8(u8"\u30b9\u30c6\u30fc\u30b8\u30af\u30ea\u30a2");
	inline const char* const StageClear = Utf8(u8"\u30b9\u30c6\u30fc\u30b8\u30af\u30ea\u30a2\uff01");
	inline const char* const RewardMoneyFormat = Utf8(u8"\u30b9\u30c6\u30fc\u30b8\u5831\u916c\uff1a+%d\u30de\u30cd\u30fc");
	inline const char* const MoneyFormat = Utf8(u8"\u6240\u6301\u91d1\uff1a%d");
	inline const char* const ClearedFormat = Utf8(u8"\u30af\u30ea\u30a2\u6570\uff1a%d");
	inline const char* const ChooseReward = Utf8(u8"\u5831\u916c\u30921\u3064\u9078\u3093\u3067\u304f\u3060\u3055\u3044");
	inline const char* const RewardNewBall = Utf8(u8"\u65b0\u3057\u3044\u30dc\u30fc\u30eb");
	inline const char* const RewardUpgrade = Utf8(u8"\u6240\u6301\u30dc\u30fc\u30eb\u3092\u5f37\u5316\uff080\u2192+1: 15\u3001+1\u2192+2: 30\u30de\u30cd\u30fc\uff09");
	inline const char* const RewardExtraMoney = Utf8(u8"\u8ffd\u52a0\u30de\u30cd\u30fc\uff08+10\uff09");
	inline const char* const NewBallSelect = Utf8(u8"\u8ffd\u52a0\u3059\u308b\u30dc\u30fc\u30eb\u3092\u30af\u30ea\u30c3\u30af\u3057\u3066\u9078\u629e");
	inline const char* const UpgradeBallSelect = Utf8(u8"\u5f37\u5316\u3059\u308b\u500b\u4f53\u3092\u30af\u30ea\u30c3\u30af\u3057\u3066\u9078\u629e");
	inline const char* const ClearControls = Utf8(u8"\u4e0a\u4e0b\u30ad\u30fc\uff1a\u5831\u916c\u3000\u5de6\u53f3\u30ad\u30fc\uff1a\u30dc\u30fc\u30eb\u3000ENTER\u30fbSPACE\uff1a\u7372\u5f97");
	inline const char* const NextRouteControls = Utf8(u8"ENTER\u30fbSPACE\uff1a\u6b21\u306e\u30eb\u30fc\u30c8\u3092\u9078\u629e");
	inline const char* const NewBallAdded = Utf8(u8"\u65b0\u3057\u3044\u30dc\u30fc\u30eb\u3092\u30c7\u30c3\u30ad\u3078\u8ffd\u52a0\u3057\u307e\u3057\u305f\u3002");
	inline const char* const NoBallAvailable = Utf8(u8"\u8ffd\u52a0\u3067\u304d\u308b\u30dc\u30fc\u30eb\u304c\u3042\u308a\u307e\u305b\u3093\u3002");
	inline const char* const UpgradeApplied = Utf8(u8"\u30dc\u30fc\u30eb\u3092\u5f37\u5316\u3057\u307e\u3057\u305f\u3002");
	inline const char* const UpgradeUnavailable = Utf8(u8"\u5f37\u5316\u3067\u304d\u308b\u30dc\u30fc\u30eb\u304c\u3042\u308a\u307e\u305b\u3093\u3002");
	inline const char* const UpgradeCostFormat = Utf8(u8"\u5f37\u5316\u8cbb\u7528\uff1a%d\u30de\u30cd\u30fc");
	inline const char* const UpgradeMoneyShortage = Utf8(u8"\u5f37\u5316\u306b\u5fc5\u8981\u306a\u30de\u30cd\u30fc\u304c\u8db3\u308a\u307e\u305b\u3093\u3002");
	inline const char* const ExtraMoneyReceived = Utf8(u8"\u8ffd\u52a0\u306710\u30de\u30cd\u30fc\u3092\u7372\u5f97\u3057\u307e\u3057\u305f\u3002");
	inline const char* const ChooseClearReward = Utf8(u8"\u30af\u30ea\u30a2\u5831\u916c\u30921\u3064\u9078\u3093\u3067\u304f\u3060\u3055\u3044\u3002");

	inline const char* const NoSave = Utf8(u8"\u30bb\u30fc\u30d6\u30c7\u30fc\u30bf\u306f\u3042\u308a\u307e\u305b\u3093");
	inline const char* const SaveValidationFailed = Utf8(u8"\u30bb\u30fc\u30d6\u30c7\u30fc\u30bf\u3092\u78ba\u8a8d\u3067\u304d\u307e\u305b\u3093");
	inline const char* const SaveSummaryFormat = Utf8(u8"\u30d5\u30ed\u30a2 %d\u3000HP %d / %d\u3000\u6240\u6301\u91d1 %d");
	inline const char* const BattleAutosaveNotice = Utf8(u8"\u6226\u95d8\u4e2d\u306e\u9032\u884c\u306f\u3001\u6226\u95d8\u958b\u59cb\u524d\u306e\u30aa\u30fc\u30c8\u30bb\u30fc\u30d6\u304b\u3089\u518d\u958b\u3067\u304d\u307e\u3059\u3002");
	inline const char* const LoadFromTitle = Utf8(u8"\u30ed\u30fc\u30c9\u306f\u30bf\u30a4\u30c8\u30eb\u753b\u9762\u304b\u3089\u5b9f\u884c\u3057\u3066\u304f\u3060\u3055\u3044\u3002");
	inline const char* const SavingUnavailable = Utf8(u8"\u3053\u306e\u753b\u9762\u3067\u306f\u30bb\u30fc\u30d6\u3067\u304d\u307e\u305b\u3093\u3002");
	inline const char* const RunSaved = Utf8(u8"\u30e9\u30f3\u3092\u30bb\u30fc\u30d6\u3057\u307e\u3057\u305f\u3002");
	inline const char* const RunLoaded = Utf8(u8"\u30bb\u30fc\u30d6\u30c7\u30fc\u30bf\u3092\u8aad\u307f\u8fbc\u307f\u307e\u3057\u305f\u3002");
	inline const char* const SaveFailedPrefix = Utf8(u8"\u30bb\u30fc\u30d6\u5931\u6557\uff1a");
	inline const char* const LoadFailedPrefix = Utf8(u8"\u30ed\u30fc\u30c9\u5931\u6557\uff1a");
	inline const char* const InvalidSaveData = Utf8(u8"\u30bb\u30fc\u30d6\u30c7\u30fc\u30bf\u306e\u5185\u5bb9\u304c\u4e0d\u6b63\u3067\u3059\u3002");
	inline const char* const UnsupportedSaveData = Utf8(u8"\u3053\u306e\u30d0\u30fc\u30b8\u30e7\u30f3\u3067\u306f\u8aad\u307f\u8fbc\u3081\u306a\u3044\u30bb\u30fc\u30d6\u30c7\u30fc\u30bf\u3067\u3059\u3002");
	inline const char* const CorruptedSaveData = Utf8(u8"\u30bb\u30fc\u30d6\u30c7\u30fc\u30bf\u304c\u7834\u640d\u3057\u3066\u3044\u307e\u3059\u3002");
	inline const char* const RandomRestoreFailed = Utf8(u8"\u4e71\u6570\u72b6\u614b\u3092\u5fa9\u5143\u3067\u304d\u307e\u305b\u3093\u3002");
	inline const char* const TemporarySaveFailed = Utf8(u8"\u4e00\u6642\u30bb\u30fc\u30d6\u30d5\u30a1\u30a4\u30eb\u3092\u4f5c\u6210\u3067\u304d\u307e\u305b\u3093\u3002");
	inline const char* const SaveWriteFailed = Utf8(u8"\u30bb\u30fc\u30d6\u30c7\u30fc\u30bf\u3092\u66f8\u304d\u8fbc\u3081\u307e\u305b\u3093\u3002");
	inline const char* const SaveCommitFailed = Utf8(u8"\u30bb\u30fc\u30d6\u30c7\u30fc\u30bf\u3092\u78ba\u5b9a\u3067\u304d\u307e\u305b\u3093\u3002");
}

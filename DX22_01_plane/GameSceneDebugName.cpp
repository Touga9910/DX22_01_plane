#include "GameSceneDebugName.h"

#include "BattleScene.h"
#include "RestSiteScene.h"
#include "ResultScene.h"
#include "ShopScene.h"
#include "StageSelectScene.h"
#include "TitleScene.h"

const char* GetSceneDebugName(Scene* scene)
{
	if (dynamic_cast<TitleScene*>(scene)) return "TITLE";
	if (dynamic_cast<StageSelectScene*>(scene)) return "SELECT";
	if (dynamic_cast<BattleScene*>(scene)) return "BATTLE";
	if (dynamic_cast<RestSiteScene*>(scene)) return "REST_SITE";
	if (dynamic_cast<ShopScene*>(scene)) return "SHOP";
	if (dynamic_cast<ResultScene*>(scene)) return "RESULT";
	return "Unknown";
}

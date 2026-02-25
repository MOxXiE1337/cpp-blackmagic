// dobby hooker, for linux x86/x86_64/arm/arm64, andorid x86/x86_64/arm/arm64
#include <cassert>
#include <Dobby/Dobby.h>

#include "cppbm/internal/hook/hooker.h"

class DobbyHooker : public cpp::blackmagic::hook::Hooker
{
public:
	bool CreateHook(void* target, void* detour, void** origin) override
	{
		return DobbyHook(target, detour, origin) == 0;
	}

	// Dobby backend doesn't have enable hook design
	bool EnableHook(void* target) override
	{
		return true;
	}

	// Dobby backend doesn't have disable hook design
	bool DisableHook(void* target) override
	{
		return true;
	}

	bool RemoveHook(void* target) override
	{
		return DobbyDestroy(target) == 0;
	}
};

cpp::blackmagic::hook::Hooker& cpp::blackmagic::hook::Hooker::GetInstance()
{
	static DobbyHooker instance{};
	return instance;
}

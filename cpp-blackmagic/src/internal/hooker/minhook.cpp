// minhook hooker, for windows x86/x86_64
#include <cassert>
#include <MinHook/MinHook.h>
#include "cppbm/internal/hook/hooker.h"

class MinHookHooker : public cpp::blackmagic::hook::Hooker
{
public:
	MinHookHooker()
	{
		const auto s = MH_Initialize();
		init_ok_ = (s == MH_OK || s == MH_ERROR_ALREADY_INITIALIZED);
	}

	~MinHookHooker() override
	{
		if (!init_ok_) return;
		(void)MH_Uninitialize();
	}

	bool CreateHook(void* target, void* detour, void** origin) override
	{
		return init_ok_ && (MH_CreateHook(target, detour, origin) == MH_OK);
	}

	bool EnableHook(void* target) override
	{
		return init_ok_ && (MH_EnableHook(target) == MH_OK);
	}

	bool DisableHook(void* target) override
	{
		return init_ok_ && (MH_DisableHook(target) == MH_OK);
	}

	bool RemoveHook(void* target) override
	{
		return init_ok_ && (MH_RemoveHook(target) == MH_OK);
	}

private:
	bool init_ok_ = false;
};

cpp::blackmagic::hook::Hooker& cpp::blackmagic::hook::Hooker::GetInstance()
{
	static MinHookHooker instance{};
	return instance;
}

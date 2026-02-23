// dobby hooker, for linux x86/x86_64, andorid x86/x86_64/arm32/arm64
#include <cassert>
#include <iostream>
#include <Dobby/Dobby.h>

#include "cppbm/hooker.h"

class DobbyHooker : public cpp::blackmagic::Hooker
{
public:
	bool CreateHook(void* target, void* detour, void** origin) override
	{
		return DobbyHook(target, detour, origin) == 0;
	}

	bool EnableHook(void* target) override
	{
		return true;
	}

	bool DisableHook(void* target) override
	{
		return true;
	}

	bool RemoveHook(void* target) override
	{
		return DobbyDestroy(target) == 0;
	}
};

cpp::blackmagic::Hooker* cpp::blackmagic::Hooker::Instance{ nullptr };

cpp::blackmagic::Hooker* cpp::blackmagic::Hooker::GetInstance()
{
	static DobbyHooker hooker_impl{};
	Instance = &hooker_impl;
	return Instance;
}

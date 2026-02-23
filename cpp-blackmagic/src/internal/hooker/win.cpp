// hooker, for windows x86/x86_64
#include <cassert>
#include <iostream>
#include <MinHook/MinHook.h>
#include "cppbm/hooker.h"

class MinHookHooker : public cpp::blackmagic::Hooker
{
public:
	MinHookHooker()
	{
		// init minhook
		assert(MH_Initialize() == MH_OK && "failed to init minhook.");
	}

	~MinHookHooker()
	{
		// uninit minhook
		assert(MH_Uninitialize() == MH_OK && "failed to uninit minhook.");
	}

	bool CreateHook(void* target, void* detour, void** origin) override
	{
		return MH_CreateHook(target, detour, origin) == MH_OK;
	}

	bool EnableHook(void* target) override
	{
		return MH_EnableHook(target) == MH_OK;
	}

	bool DisableHook(void* target) override
	{
		return MH_DisableHook(target) == MH_OK;
	}

	bool RemoveHook(void* target) override
	{
		return MH_RemoveHook(target) == MH_OK;
	}
};

cpp::blackmagic::Hooker* cpp::blackmagic::Hooker::Instance{ nullptr };

cpp::blackmagic::Hooker* cpp::blackmagic::Hooker::GetInstance()
{
	static MinHookHooker hooker_impl{};
	Instance = &hooker_impl;
	return Instance;
}

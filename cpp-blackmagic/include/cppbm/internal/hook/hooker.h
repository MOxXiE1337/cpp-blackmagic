#ifndef __CPPBM_HOOK_HOOKER_H__
#define __CPPBM_HOOK_HOOKER_H__

namespace cpp::blackmagic::hook
{
	class Hooker
	{
	public:
		virtual ~Hooker() = default;

		virtual bool CreateHook(void* target, void* detour, void** origin) = 0;
		virtual bool EnableHook(void* target) = 0;
		virtual bool DisableHook(void* target) = 0;
		virtual bool RemoveHook(void* target) = 0;

	public:
		static Hooker& GetInstance();
	};
}

#endif // __CPPBM_HOOK_HOOKER_H__

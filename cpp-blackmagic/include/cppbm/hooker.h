#ifndef __CPPBM_HOOKER_H__
#define __CPPBM_HOOKER_H__

namespace cpp::blackmagic
{
	class Hooker
	{
	public:
		virtual bool CreateHook(void* target, void* detour, void** origin) = 0;
		virtual bool EnableHook(void* target) = 0;
		virtual bool DisableHook(void* target) = 0;
		virtual bool RemoveHook(void* target) = 0;
	public:
		static Hooker* GetInstance();
		static Hooker* Instance; 
	};
}

#endif // __CPPBM_HOOKER_H__

// YES! YOU CAN ACTUALLY USE THIS TO IMPL A INLINE HOOK!
#include <Windows.h>
#include <cppbm/decorator.h>


class MessageBoxAHooker : public cpp::blackmagic::AutoDecorator<MessageBoxA>
{
public:
	// optional, you can bind this decorator to another funciton ptr
	// using MessageBoxAType = decltype(&MessageBoxA);
	// explicit MessageBoxAHooker(MessageBoxAType target = MessageBoxA) : cpp::blackmagic::AutoDecorator<MessageBoxA>(target) {}

	int Call(HWND hwnd, LPCSTR text, LPCSTR caption, UINT type)
	{
		return CallOriginal(hwnd, "DECORATOR HIJACKED THE FUNCTION!", caption, type | MB_ICONINFORMATION);
	}
};

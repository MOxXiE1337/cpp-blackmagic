#ifndef __CPPBM_NONCOPYABLE_H__
#define __CPPBM_NONCOPYABLE_H__

namespace cpp::blackmagic
{
	struct NonCopyable
	{
	protected:
		NonCopyable() = default;
		~NonCopyable() = default;

	public:
		NonCopyable(const NonCopyable&) = delete;
		NonCopyable& operator=(const NonCopyable&) = delete;
		NonCopyable(NonCopyable&&) = delete;
		NonCopyable& operator=(NonCopyable&&) = delete;
	};
}

#endif // __CPPBM_NONCOPYABLE_H__

#ifndef __CPPBM_HOOK_ARG_SLOT_H__
#define __CPPBM_HOOK_ARG_SLOT_H__

#include <memory>
#include <type_traits>
#include <utility>

namespace cpp::blackmagic::hook
{
    template <typename Arg>
    using ArgStorageT = std::conditional_t<
        std::is_reference_v<Arg>,
        std::add_pointer_t<std::remove_reference_t<Arg>>,
        std::remove_cv_t<Arg>>;

    template <typename Arg>
    class ArgSlot
    {
    public:
        static_assert(!std::is_reference_v<Arg>,
            "ArgSlot<Arg> primary template only accepts non-reference Arg.");

        using Value = std::remove_cv_t<Arg>;

        explicit ArgSlot(Value& storage) : storage_(&storage)
        {
        }

        Value& Get() const
        {
            return *storage_;
        }

        Value& BeforeArg() const
        {
            return *storage_;
        }

        void Assign(Value value) const
        {
            *storage_ = std::move(value);
        }

    private:
        Value* storage_ = nullptr;
    };

    template <typename Ref>
    class ArgSlotReferenceBase
    {
    public:
        using Raw = std::remove_reference_t<Ref>;
        using Ptr = Raw*;

        explicit ArgSlotReferenceBase(Ptr& storage) : storage_(&storage)
        {
        }

        Raw& Get() const
        {
            return **storage_;
        }

        Raw& BeforeArg() const
        {
            return **storage_;
        }

        Ptr Pointer() const
        {
            return *storage_;
        }

        void Rebind(Ptr ptr) const
        {
            *storage_ = ptr;
        }

        void Rebind(Raw& ref) const
        {
            *storage_ = std::addressof(ref);
        }

    private:
        Ptr* storage_ = nullptr;
    };

    template <typename T>
    class ArgSlot<T&> : public ArgSlotReferenceBase<T&>
    {
    public:
        using ArgSlotReferenceBase<T&>::ArgSlotReferenceBase;
    };

    template <typename T>
    class ArgSlot<T&&> : public ArgSlotReferenceBase<T&&>
    {
    public:
        using ArgSlotReferenceBase<T&&>::ArgSlotReferenceBase;
    };

    template <typename Arg>
    ArgStorageT<Arg> InitArgStorage(Arg& arg)
    {
        if constexpr (std::is_reference_v<Arg>)
        {
            return std::addressof(arg);
        }
        else
        {
            return arg;
        }
    }

    template <typename Arg>
    decltype(auto) ForwardCallArg(ArgStorageT<Arg>& storage)
    {
        if constexpr (std::is_lvalue_reference_v<Arg>)
        {
            return *storage;
        }
        else if constexpr (std::is_rvalue_reference_v<Arg>)
        {
            return std::move(*storage);
        }
        else
        {
            return storage;
        }
    }
}

#endif // __CPPBM_HOOK_ARG_SLOT_H__

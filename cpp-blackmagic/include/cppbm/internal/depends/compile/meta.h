// File role:
// This header hosts lightweight DI meta utilities:
// - common type aliases
// - marker traits for Depends maker types
// - conversion helper that builds pointer metadata from Depends expressions
//
// Keeping this in a dedicated file helps isolate template meta logic
// from runtime context/cache code.

#ifndef __CPPBM_DEPENDS_META_H__
#define __CPPBM_DEPENDS_META_H__

#include <type_traits>
#include <utility>

#include "invoke.h"
#include "registry.h"

namespace cpp::blackmagic::depends
{
    // Remove cv/ref qualifiers in one alias.
    // Used across resolve and conversion code paths.
    template <typename T>
    using RemoveCvRefT = std::remove_cvref_t<T>;

    // Detect std::reference_wrapper<T>.
    // Explicit injection treats it as a borrowed reference handle.
    template <typename T>
    struct IsReferenceWrapper : std::false_type {};

    template <typename T>
    struct IsReferenceWrapper<std::reference_wrapper<T>> : std::true_type {};

    // Forward declaration for Depends maker type.
	// Full definition is in runtime/placeholder.h.
	template <typename FactoryReturn>
	struct DependsMaker;

    // Trait: whether type is DependsMaker<...>.
	template <typename T>
	struct IsDependsMaker : std::false_type {};

    template <typename FactoryReturn>
    struct IsDependsMaker<DependsMaker<FactoryReturn>> : std::true_type {};

    // Trait: whether maker carries an explicit factory.
    template <typename T>
    struct IsDependsFactoryMaker : std::false_type
    {
        static constexpr bool kFactoryReturnsPointer = false;
    };

    template <typename FactoryReturn>
    struct IsDependsFactoryMaker<DependsMaker<FactoryReturn>>
        : std::bool_constant<!std::is_void_v<FactoryReturn>>
    {
        static constexpr bool kFactoryReturnsPointer =
            !std::is_void_v<FactoryReturn> && kFactoryProducesPointerV<FactoryReturn>;
    };

    // Build pointer metadata for generated default-arg registration.
	//
	// Why this exists:
	// - inject.py emits MakeDependsPtrValue<Raw>(Depends(...))
	// - this function converts Depends(...) expression into:
	//   DependsPtrValue<Raw>{ ptr, owned, factory, cached }
	//
	// Ownership decision:
	// - Depends(factory) where factory returns Raw*  => owned = true
	// - Depends(factory) where factory returns Raw&  => owned = false
	// - Depends()                                    => owned = false
	//
	// Strict input contract:
	// - Only Depends() / Depends(factory) expressions are accepted.
	template <typename Raw, typename Expr>
	DependsPtrValue<Raw> MakeDependsPtrValue(Expr&& expr)
	{
		using E = RemoveCvRefT<Expr>;
		Raw* ptr = nullptr;
		const void* factory = nullptr;
		bool cached = true;
		if constexpr (IsDependsFactoryMaker<E>::value)
		{
			E maker = std::forward<Expr>(expr);
			factory = FactoryKeyOf(maker.factory);
			cached = maker.cached;
			decltype(auto) produced = InvokeFactory(maker.factory);
			ptr = ConvertFactoryResult<Raw*>(std::forward<decltype(produced)>(produced));
		}
		else if constexpr (IsDependsMaker<E>::value)
		{
			E maker = std::forward<Expr>(expr);
			cached = maker.cached;
			if constexpr (std::is_convertible_v<E&&, Raw*>)
			{
				ptr = static_cast<Raw*>(std::forward<E>(maker));
			}
			else if constexpr (std::is_convertible_v<E&&, Raw&>)
			{
				ptr = std::addressof(static_cast<Raw&>(std::forward<E>(maker)));
			}
			else
			{
				static_assert(std::is_same_v<Raw, void>,
					"Depends() expression cannot be converted to requested dependency raw type.");
			}
		}
		else
			{
				static_assert(std::is_same_v<Raw, void>,
					"MakeDependsPtrValue only accepts Depends() or Depends(factory) expressions.");
			}
		// Ownership must track factory source category, not conversion target.
		// Example:
		// - factory returns Raw& and parameter expects Raw* => conversion is allowed,
		//   but ownership must remain borrowed (owned = false).
		// - factory returns Raw* => owned = true.
		const bool owned =
			IsDependsFactoryMaker<E>::value &&
			IsDependsFactoryMaker<E>::kFactoryReturnsPointer;
		return DependsPtrValue<Raw>{ ptr, owned, factory, cached };
	}

	template <typename Param>
	using DependsRawFromParamT =
		std::remove_cv_t<std::remove_pointer_t<std::remove_reference_t<Param>>>;

	template <typename Param, typename Expr>
	auto MakeDefaultArgMetadata(Expr&& expr)
	{
		using E = RemoveCvRefT<Expr>;
		if constexpr (IsDependsMaker<E>::value)
		{
			using Raw = DependsRawFromParamT<Param>;
			return MakeDependsPtrValue<Raw>(std::forward<Expr>(expr));
		}
		else
		{
			static_assert(
				std::is_same_v<E, void>,
				"MakeDefaultArgMetadata only accepts Depends() or Depends(factory) expressions.");
		}
	}

	// Deduce concrete metadata value type produced by one default expression.
	// Used by async metadata builder to keep exact storage type compatible with
	// existing InjectRegistry registration and lookup rules.
	template <typename Param, typename Expr>
	using DefaultArgMetadataTypeT =
		decltype(MakeDefaultArgMetadata<Param>(std::declval<Expr>()));
}

#endif // __CPPBM_DEPENDS_META_H__

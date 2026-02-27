#include "cases.h"

#include <cppbm/decorator.h>

#include <cstdio>
#include <iostream>

namespace cppbm::examples::decorator
{
    using namespace cpp::blackmagic;

    template <auto Target>
    class RouteDecorator;

    template <typename R, typename... Args, R(*Target)(Args...)>
    class RouteDecorator<Target> : public FunctionDecorator<Target>
    {
    public:
        bool BeforeCall(Args&... /*args*/) override
        {
            std::printf("[route.before] ");
            return true;
        }

        void AfterCall(R& /*result*/) override
        {
            std::printf("[route.after] ");
        }
    };

    struct RouteBindingExpr
    {
        template <auto Target>
        auto Bind() const
        {
            return DecoratorBinding<Target, RouteDecorator>{};
        }
    };

    struct App
    {
        RouteBindingExpr get(const char* /*path*/) const
        {
            return {};
        }
    };

    inline constexpr App app{};

    decorator(@app.get("/health"))
    int HealthStatus();

    int HealthStatus()
    {
        return 200;
    }

    void RunDecoratorCaseExpression()
    {
        std::cout << "[expression] HealthStatus() => " << HealthStatus() << "\n";
    }
}

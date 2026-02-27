#include "cases.h"

#include <cppbm/decorator.h>

#include <cstdio>
#include <iostream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

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

    class App;

    struct RouteBindingExpr
    {
        App* app = nullptr;
        const char* path = "";

        template <auto Target, typename... Metas>
        auto Bind(Metas&&... metas) const;

    private:
        template <typename Invoker, typename Meta>
        static void TryPickInvoker(Invoker& out, Meta&& meta)
        {
            using MetaT = std::decay_t<Meta>;
            if constexpr (std::is_convertible_v<MetaT, Invoker>)
            {
                out = static_cast<Invoker>(std::forward<Meta>(meta));
            }
        }
    };

    class App
    {
    public:
        using RouteInvoker = int(*)();

        RouteBindingExpr get(const char* path)
        {
            return RouteBindingExpr{ this, path };
        }

        void Register(const char* path, RouteInvoker invoker)
        {
            if (path == nullptr || invoker == nullptr)
            {
                return;
            }
            routes_[path] = invoker;
        }

        int Invoke(const char* path) const
        {
            if (path == nullptr)
            {
                return -1;
            }
            auto it = routes_.find(path);
            if (it == routes_.end() || it->second == nullptr)
            {
                return -1;
            }
            return it->second();
        }

    private:
        std::unordered_map<std::string, RouteInvoker> routes_{};
    };

    template <auto Target, typename... Metas>
    auto RouteBindingExpr::Bind(Metas&&... metas) const
    {
        using Invoker = App::RouteInvoker;
        Invoker selected = nullptr;
        (TryPickInvoker<Invoker>(selected, std::forward<Metas>(metas)), ...);
        if (app != nullptr)
        {
            app->Register(path, selected);
        }
        return DecoratorBinding<Target, RouteDecorator>{};
    }

    inline App app{};

                                  
    int HealthStatus();

    int HealthStatus()
    {
        return 200;
    }

    void RunDecoratorCaseExpression()
    {
        std::cout << "[expression] HealthStatus() => " << HealthStatus() << "\n";
        std::cout << "[expression] app.Invoke(\"/health\") => " << app.Invoke("/health") << "\n";
    }
}



// Generated decorator bindings.
namespace cppbm::examples::decorator {
inline auto __cppbm_dec_HealthStatus_2580_0 = (app.get("/health")).Bind<&cppbm::examples::decorator::HealthStatus>(
    []() { return ::cppbm::examples::decorator::HealthStatus(); }
);
}

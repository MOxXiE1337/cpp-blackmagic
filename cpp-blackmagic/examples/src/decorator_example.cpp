#include <cppbm/decorator.h>

#include <iostream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

using namespace cpp::blackmagic;

template <auto Target>
class decorator_class(add_one);

template <typename R, typename... Args, R(*Target)(Args...)>
class decorator_class(add_one)<Target> : public FunctionDecorator<Target>
{
public:
    R Call(Args... args) override
    {
        if constexpr (std::is_void_v<R>)
        {
            this->CallOriginal(args...);
            return;
        }
        else
        {
            return this->CallOriginal(args...) + static_cast<R>(1);
        }
    }
};

class Router
{
public:
    class RouteBinder
    {
    public:
        RouteBinder(Router* router, std::string method, std::string path)
            : router_(router), method_(std::move(method)), path_(std::move(path))
        {
        }

        template <auto Target>
        bool bind() const
        {
            return router_->Register(method_, path_, TargetKey<Target>());
        }

    private:
        template <auto Target>
        static const void* TargetKey()
        {
            static int token = 0;
            return &token;
        }

        Router* router_ = nullptr;
        std::string method_{};
        std::string path_{};
    };

    RouteBinder get(const char* path)
    {
        return RouteBinder{ this, "GET", path };
    }

    bool HasRoute(const std::string& method, const std::string& path) const
    {
        return routes_.find(method + " " + path) != routes_.end();
    }

private:
    bool Register(const std::string& method, const std::string& path, const void* handler_key)
    {
        return routes_.emplace(method + " " + path, handler_key).second;
    }

    std::unordered_map<std::string, const void*> routes_{};
};

inline Router router{};

decorator(@add_one)
int add(int a, int b)
{
    return a + b;
}

decorator(@router.get("/health"))
int health()
{
    return 200;
}

int main()
{
    std::cout << "[decorator] add(2, 3) => " << add(2, 3) << "\n";
    std::cout << "[class decorator] health() => " << health() << "\n";
    std::cout << "[class decorator] route GET /health registered: "
              << (router.HasRoute("GET", "/health") ? "true" : "false") << "\n";
    return 0;
}

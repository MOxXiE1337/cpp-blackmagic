#include <cppbm/depends.h>
#include <cppbm/internal/depends/coroutine.h>

// Demo role:
// - basic @inject route wiring
// - async route handlers with Task<T>
// - Depends(factory) with pointer factory
// - Depends(async factory) with Task<T*> and Task<T&>

#include <iostream>
#include <string>
#include <unordered_map>

using namespace cpp::blackmagic;

struct RequestContext
{
    std::string request_id{};
    std::string token{};
    int user_id = 0;
};

struct HealthResponseConfig
{
    int code = 200;
    std::string banner = "ok";
};

struct ILogger
{
    virtual ~ILogger() = default;
    virtual void Info(const std::string& message) = 0;
};

struct IAuthService
{
    virtual ~IAuthService() = default;
    virtual Task<bool> ValidateAsync(const std::string& token) = 0;
};

struct IUserRepository
{
    virtual ~IUserRepository() = default;
    virtual Task<std::string> GetUserNameAsync(int user_id) = 0;
};

class ConsoleLogger final : public ILogger
{
public:
    void Info(const std::string& message) override
    {
        std::cout << "[log] " << message << "\n";
    }
};

class DemoAuthService final : public IAuthService
{
public:
    Task<bool> ValidateAsync(const std::string& token) override
    {
        // Deliberately asynchronous shape:
        // route code can co_await even though this demo does not use real IO.
        co_return token == "allow";
    }
};

class InMemoryUserRepository final : public IUserRepository
{
public:
    Task<std::string> GetUserNameAsync(int user_id) override
    {
        auto it = users_.find(user_id);
        if (it == users_.end())
        {
            co_return "unknown";
        }
        co_return it->second;
    }

private:
    std::unordered_map<int, std::string> users_{
        {1, "alice"},
        {2, "bob"},
        {7, "charlie"},
    };
};

RequestContext& CurrentRequestContextFactory()
{
    static thread_local RequestContext fallback{
        "fallback-request",
        "deny",
        0
    };
    return fallback;
}

IAuthService& AuthFactory()
{
    static DemoAuthService service{};
    return service;
}

IUserRepository& UserRepoFactory()
{
    static InMemoryUserRepository repo{};
    return repo;
}

ILogger& LoggerFactory()
{
    static ConsoleLogger logger{};
    return logger;
}

HealthResponseConfig* HealthConfigFactory()
{
    // Pointer returned from factory is owned by Depends context.
    // Returning heap object here is intentional.
    return new HealthResponseConfig{
        200,
        "healthy (from pointer factory)"
    };
}

Task<HealthResponseConfig*> AsyncHealthConfigFactory()
{
    // Async factory is supported by Depends:
    // resolver will call Get() on task-like return and then inject the result.
    co_return new HealthResponseConfig{
        201,
        "healthy (from async Task factory)"
    };
}

Task<HealthResponseConfig&> AsyncHealthConfigRefFactory()
{
    // Task<T&> async factory case:
    // Depends can consume Task<HealthResponseConfig&> and adapt to reference/pointer params.
    static HealthResponseConfig cfg{
        202,
        "healthy (from async Task<T&> factory)"
    };
    co_return cfg;
}

decorator(@inject)
Task<> LogRequest(
    ILogger* logger = Depends(LoggerFactory),
    RequestContext* ctx = Depends(CurrentRequestContextFactory))
{
    logger->Info("req=" + ctx->request_id + " user_id=" + std::to_string(ctx->user_id));
    co_return;
}

decorator(@inject)
Task<bool> EnsureAuthorized(
    IAuthService* auth = Depends(AuthFactory),
    RequestContext* ctx = Depends(CurrentRequestContextFactory))
{
    const bool ok = co_await auth->ValidateAsync(ctx->token);
    co_return ok;
}

decorator(@inject)
Task<int> ResolveUserId(
    RequestContext* ctx = Depends(CurrentRequestContextFactory))
{
    // Task<int> example.
    co_return ctx->user_id;
}

decorator(@inject)
Task<std::string> HandleGetUser(
    IUserRepository* repo = Depends(UserRepoFactory),
    ILogger* logger = Depends(LoggerFactory))
{
    co_await LogRequest();

    const bool authorized = co_await EnsureAuthorized();
    if (!authorized)
    {
        logger->Info("request rejected: unauthorized");
        co_return "HTTP 401 unauthorized";
    }

    const int user_id = co_await ResolveUserId();
    const std::string user_name = co_await repo->GetUserNameAsync(user_id);
    co_return "HTTP 200 user=" + user_name;
}

decorator(@inject)
Task<std::string> HandleHealth(
    HealthResponseConfig* cfg = Depends(HealthConfigFactory),
    ILogger* logger = Depends(LoggerFactory))
{
    logger->Info("health route uses pointer dependency factory");
    co_return "HTTP " + std::to_string(cfg->code) + " " + cfg->banner;
}

decorator(@inject)
Task<std::string> HandleAsyncHealth(
    HealthResponseConfig* cfg = Depends(AsyncHealthConfigFactory),
    ILogger* logger = Depends(LoggerFactory))
{
    logger->Info("health route uses async Task dependency factory");
    co_return "HTTP " + std::to_string(cfg->code) + " " + cfg->banner;
}

decorator(@inject)
Task<std::string> HandleAsyncHealthRef(
    HealthResponseConfig& cfg = Depends(AsyncHealthConfigRefFactory),
    ILogger* logger = Depends(LoggerFactory))
{
    logger->Info("health route uses async Task<T&> dependency factory");
    co_return "HTTP " + std::to_string(cfg.code) + " " + cfg.banner;
}

int main()
{
    (void)ClearDependencies();

    RequestContext ok_req{
        "req-1001",
        "allow",
        7
    };
    {
        auto guard = ScopeOverrideDependency(&ok_req, CurrentRequestContextFactory);
        std::cout << "[resp] " << HandleGetUser().Get() << "\n";
    }

    RequestContext denied_req{
        "req-1002",
        "deny",
        2
    };
    {
        auto guard = ScopeOverrideDependency(&denied_req, CurrentRequestContextFactory);
        std::cout << "[resp] " << HandleGetUser().Get() << "\n";
    }

    std::cout << "[resp] " << HandleHealth().Get() << "\n";
    std::cout << "[resp] " << HandleAsyncHealth().Get() << "\n";
    std::cout << "[resp] " << HandleAsyncHealthRef().Get() << "\n";

    HealthResponseConfig forced{
        299,
        "forced-health-config (override)"
    };
    {
        auto guard = ScopeOverrideDependency(&forced, HealthConfigFactory);
        std::cout << "[resp] " << HandleHealth().Get() << "\n";
    }

    return 0;
}

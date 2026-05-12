#include <drogon/drogon.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace
{
constexpr const char *kSessionCookieName = "aerosentinel_session";

struct AuthConfig
{
    std::string username;
    std::string password;
    bool secureCookies{false};
};

class SessionStore
{
  public:
    std::string create()
    {
        std::string token = makeToken();
        const auto expiresAt = std::chrono::steady_clock::now() + ttl_;

        std::lock_guard<std::mutex> lock(mutex_);
        sessions_[token] = expiresAt;
        return token;
    }

    bool isValid(const std::string &token)
    {
        if (token.empty())
        {
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        const auto match = sessions_.find(token);
        if (match == sessions_.end())
        {
            return false;
        }

        const auto now = std::chrono::steady_clock::now();
        if (match->second <= now)
        {
            sessions_.erase(match);
            return false;
        }

        match->second = now + ttl_;
        return true;
    }

    void destroy(const std::string &token)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_.erase(token);
    }

  private:
    static std::string makeToken()
    {
        std::array<unsigned char, 32> bytes{};
        std::random_device random;
        for (auto &byte : bytes)
        {
            byte = static_cast<unsigned char>(random());
        }

        std::ostringstream token;
        token << std::hex << std::setfill('0');
        for (const auto byte : bytes)
        {
            token << std::setw(2) << static_cast<int>(byte);
        }
        return token.str();
    }

    std::mutex mutex_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> sessions_;
    std::chrono::hours ttl_{8};
};

std::filesystem::path executableDir(const char *argv0)
{
    if (argv0 == nullptr || *argv0 == '\0')
    {
        return std::filesystem::current_path();
    }

    std::error_code ec;
    auto path = std::filesystem::absolute(argv0, ec);
    if (ec)
    {
        return std::filesystem::current_path();
    }

    return path.parent_path();
}

std::filesystem::path findPublicDir(const char *argv0)
{
    const auto exeDir = executableDir(argv0);
    const std::vector<std::filesystem::path> candidates = {
        std::filesystem::current_path() / "public",
        exeDir / "public",
        exeDir.parent_path() / "public"};

    for (const auto &candidate : candidates)
    {
        std::error_code ec;
        if (std::filesystem::exists(candidate / "index.html", ec))
        {
            return std::filesystem::weakly_canonical(candidate, ec);
        }
    }

    return std::filesystem::current_path() / "public";
}

uint16_t portFromEnvironment()
{
    const char *portValue = std::getenv("PORT");
    if (portValue == nullptr)
    {
        return 8080;
    }

    try
    {
        const int parsed = std::stoi(portValue);
        if (parsed > 0 && parsed <= 65535)
        {
            return static_cast<uint16_t>(parsed);
        }
    }
    catch (...)
    {
    }

    return 8080;
}

std::string envOrDefault(const char *name, const char *fallback)
{
    const char *value = std::getenv(name);
    if (value == nullptr || *value == '\0')
    {
        return fallback;
    }
    return value;
}

bool envFlag(const char *name)
{
    std::string value = envOrDefault(name, "");
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    return value == "1" || value == "true" || value == "yes" || value == "on";
}

bool constantTimeEquals(const std::string &left, const std::string &right)
{
    unsigned char diff = static_cast<unsigned char>(left.size() ^ right.size());
    const auto length = left.size() > right.size() ? left.size() : right.size();
    for (size_t index = 0; index < length; ++index)
    {
        const auto leftChar = index < left.size() ? static_cast<unsigned char>(left[index]) : 0;
        const auto rightChar = index < right.size() ? static_cast<unsigned char>(right[index]) : 0;
        diff = static_cast<unsigned char>(diff | (leftChar ^ rightChar));
    }
    return diff == 0;
}

int hexValue(char ch)
{
    if (ch >= '0' && ch <= '9')
    {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f')
    {
        return 10 + ch - 'a';
    }
    if (ch >= 'A' && ch <= 'F')
    {
        return 10 + ch - 'A';
    }
    return -1;
}

std::string urlDecode(std::string_view value)
{
    std::string decoded;
    decoded.reserve(value.size());

    for (size_t index = 0; index < value.size(); ++index)
    {
        if (value[index] == '+')
        {
            decoded.push_back(' ');
            continue;
        }

        if (value[index] == '%' && index + 2 < value.size())
        {
            const int high = hexValue(value[index + 1]);
            const int low = hexValue(value[index + 2]);
            if (high >= 0 && low >= 0)
            {
                decoded.push_back(static_cast<char>((high << 4) | low));
                index += 2;
                continue;
            }
        }

        decoded.push_back(value[index]);
    }

    return decoded;
}

std::string formValue(std::string_view body, std::string_view key)
{
    size_t start = 0;
    while (start <= body.size())
    {
        const auto ampersand = body.find('&', start);
        const auto end = ampersand == std::string_view::npos ? body.size() : ampersand;
        const auto pair = body.substr(start, end - start);
        const auto equals = pair.find('=');

        const auto name = urlDecode(equals == std::string_view::npos ? pair : pair.substr(0, equals));
        if (name == key)
        {
            return urlDecode(equals == std::string_view::npos ? std::string_view{} : pair.substr(equals + 1));
        }

        if (ampersand == std::string_view::npos)
        {
            break;
        }
        start = ampersand + 1;
    }

    return {};
}

std::string trim(std::string_view value)
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
    {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
    {
        value.remove_suffix(1);
    }
    return std::string(value);
}

std::string cookieValue(const drogon::HttpRequestPtr &request, const std::string &name)
{
    auto header = request->getHeader("cookie");
    if (header.empty())
    {
        header = request->getHeader("Cookie");
    }

    size_t start = 0;
    while (start <= header.size())
    {
        const auto semicolon = header.find(';', start);
        const auto end = semicolon == std::string::npos ? header.size() : semicolon;
        const auto pair = std::string_view(header).substr(start, end - start);
        const auto equals = pair.find('=');

        if (equals != std::string_view::npos && trim(pair.substr(0, equals)) == name)
        {
            return trim(pair.substr(equals + 1));
        }

        if (semicolon == std::string::npos)
        {
            break;
        }
        start = semicolon + 1;
    }

    return {};
}

std::string sessionCookie(const std::string &token, bool secure)
{
    std::string cookie = std::string(kSessionCookieName) + "=" + token +
                         "; Path=/; HttpOnly; SameSite=Strict; Max-Age=28800";
    if (secure)
    {
        cookie += "; Secure";
    }
    return cookie;
}

std::string expiredSessionCookie(bool secure)
{
    std::string cookie = std::string(kSessionCookieName) +
                         "=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0";
    if (secure)
    {
        cookie += "; Secure";
    }
    return cookie;
}

drogon::HttpResponsePtr redirectTo(const std::string &location)
{
    auto response = drogon::HttpResponse::newHttpResponse();
    response->setStatusCode(drogon::k302Found);
    response->addHeader("Location", location);
    return response;
}

bool isAuthenticated(const drogon::HttpRequestPtr &request,
                     const std::shared_ptr<SessionStore> &sessions)
{
    return sessions->isValid(cookieValue(request, kSessionCookieName));
}

drogon::HttpResponsePtr loginPage(bool showError)
{
    const std::string error = showError
                                  ? "<p class=\"auth-error\">Invalid username or password.</p>"
                                  : "";

    auto response = drogon::HttpResponse::newHttpResponse();
    response->addHeader("Content-Type", "text/html; charset=utf-8");
    response->setBody(
        "<!doctype html>"
        "<html lang=\"en\">"
        "<head>"
        "<meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<title>AeroSentinel Login</title>"
        "<link rel=\"stylesheet\" href=\"/styles.css\">"
        "</head>"
        "<body class=\"auth-body\">"
        "<main class=\"auth-shell\">"
        "<section class=\"auth-panel\">"
        "<div class=\"auth-brand\">"
        "<div class=\"brand-mark\" aria-hidden=\"true\"><span></span></div>"
        "<div><strong>AeroSentinel</strong><small>CONTROL CENTER</small></div>"
        "</div>"
        "<p class=\"auth-kicker\">Secure mission access</p>"
        "<h1>Operator Sign In</h1>"
        "<form class=\"auth-form\" method=\"post\" action=\"/login\">"
        "<label>Username<input type=\"text\" name=\"username\" autocomplete=\"username\" required autofocus></label>"
        "<label>Password<input type=\"password\" name=\"password\" autocomplete=\"current-password\" required></label>" +
        error +
        "<button type=\"submit\">Unlock Dashboard</button>"
        "</form>"
        "</section>"
        "</main>"
        "</body>"
        "</html>");
    return response;
}
} // namespace

int main(int argc, char *argv[])
{
    const auto publicDir = findPublicDir(argc > 0 ? argv[0] : nullptr);
    const auto indexPath = publicDir / "index.html";
    const auto port = portFromEnvironment();
    const auto bindAddress = envOrDefault("AEROSENTINEL_BIND_ADDRESS", "0.0.0.0");
    const AuthConfig auth{
        envOrDefault("AEROSENTINEL_USER", "admin"),
        envOrDefault("AEROSENTINEL_PASSWORD", "admin"),
        envFlag("AEROSENTINEL_SECURE_COOKIES")};
    auto sessions = std::make_shared<SessionStore>();

    if (std::getenv("AEROSENTINEL_PASSWORD") == nullptr)
    {
        LOG_WARN << "AEROSENTINEL_PASSWORD is not set; using development password 'admin'.";
    }

    auto &app = drogon::app();
    app.setDocumentRoot(publicDir.string());
    app.setLogLevel(trantor::Logger::kWarn);
    app.addListener(bindAddress, port);

    app.registerHandler(
        "/",
        [indexPath, sessions](const drogon::HttpRequestPtr &request,
                              std::function<void(const drogon::HttpResponsePtr &)> &&callback) {
            if (!isAuthenticated(request, sessions))
            {
                callback(redirectTo("/login"));
                return;
            }

            auto response = drogon::HttpResponse::newFileResponse(indexPath.string());
            callback(response);
        },
        {drogon::Get});

    app.registerHandler(
        "/mission/alpha-0426",
        [indexPath, sessions](const drogon::HttpRequestPtr &request,
                              std::function<void(const drogon::HttpResponsePtr &)> &&callback) {
            if (!isAuthenticated(request, sessions))
            {
                callback(redirectTo("/login"));
                return;
            }

            auto response = drogon::HttpResponse::newFileResponse(indexPath.string());
            callback(response);
        },
        {drogon::Get});

    app.registerHandler(
        "/index.html",
        [indexPath, sessions](const drogon::HttpRequestPtr &request,
                              std::function<void(const drogon::HttpResponsePtr &)> &&callback) {
            if (!isAuthenticated(request, sessions))
            {
                callback(redirectTo("/login"));
                return;
            }

            auto response = drogon::HttpResponse::newFileResponse(indexPath.string());
            callback(response);
        },
        {drogon::Get});

    app.registerHandler(
        "/login",
        [sessions](const drogon::HttpRequestPtr &request,
                   std::function<void(const drogon::HttpResponsePtr &)> &&callback) {
            if (isAuthenticated(request, sessions))
            {
                callback(redirectTo("/mission/alpha-0426"));
                return;
            }

            callback(loginPage(request->getParameter("error") == "1"));
        },
        {drogon::Get});

    app.registerHandler(
        "/login",
        [auth, sessions](const drogon::HttpRequestPtr &request,
                         std::function<void(const drogon::HttpResponsePtr &)> &&callback) {
            const auto body = std::string_view(request->body().data(), request->body().size());
            const auto username = formValue(body, "username");
            const auto password = formValue(body, "password");

            if (constantTimeEquals(username, auth.username) &&
                constantTimeEquals(password, auth.password))
            {
                auto response = redirectTo("/mission/alpha-0426");
                response->addHeader("Set-Cookie", sessionCookie(sessions->create(), auth.secureCookies));
                callback(response);
                return;
            }

            callback(redirectTo("/login?error=1"));
        },
        {drogon::Post});

    app.registerHandler(
        "/logout",
        [auth, sessions](const drogon::HttpRequestPtr &request,
                         std::function<void(const drogon::HttpResponsePtr &)> &&callback) {
            sessions->destroy(cookieValue(request, kSessionCookieName));
            auto response = redirectTo("/login");
            response->addHeader("Set-Cookie", expiredSessionCookie(auth.secureCookies));
            callback(response);
        },
        {drogon::Get, drogon::Post});

    app.registerHandler(
        "/api/mission/alpha-0426",
        [sessions](const drogon::HttpRequestPtr &request,
                   std::function<void(const drogon::HttpResponsePtr &)> &&callback) {
            if (!isAuthenticated(request, sessions))
            {
                Json::Value error;
                error["error"] = "authentication_required";
                auto response = drogon::HttpResponse::newHttpJsonResponse(error);
                response->setStatusCode(drogon::k401Unauthorized);
                callback(response);
                return;
            }

            Json::Value mission;
            mission["id"] = "ALPHA-0426";
            mission["status"] = "ACTIVE";
            mission["profile"] = "Search & Inspect";
            mission["survey"] = "Ridge Line Survey";
            mission["drone"] = "Sentinel-7B";
            mission["battery"] = 78;
            mission["altitude_m"] = 512;
            mission["speed_ms"] = 15.2;
            mission["distance_km"] = 1.2;
            mission["signal_percent"] = 94;
            mission["version"] = AEROSENTINEL_VERSION;

            auto response = drogon::HttpResponse::newHttpJsonResponse(mission);
            callback(response);
        },
        {drogon::Get});

    LOG_WARN << "AeroSentinel Control Center listening on http://" << bindAddress << ":" << port
             << " with document root " << publicDir.string();
    app.run();
}

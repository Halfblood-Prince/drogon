#include <drogon/drogon.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
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
} // namespace

int main(int argc, char *argv[])
{
    const auto publicDir = findPublicDir(argc > 0 ? argv[0] : nullptr);
    const auto indexPath = publicDir / "index.html";
    const auto port = portFromEnvironment();

    auto &app = drogon::app();
    app.setDocumentRoot(publicDir.string());
    app.setLogLevel(trantor::Logger::kWarn);
    app.addListener("0.0.0.0", port);

    app.registerHandler(
        "/",
        [indexPath](const drogon::HttpRequestPtr &,
                    std::function<void(const drogon::HttpResponsePtr &)> &&callback) {
            auto response = drogon::HttpResponse::newFileResponse(indexPath.string());
            callback(response);
        },
        {drogon::Get});

    app.registerHandler(
        "/mission/alpha-0426",
        [indexPath](const drogon::HttpRequestPtr &,
                    std::function<void(const drogon::HttpResponsePtr &)> &&callback) {
            auto response = drogon::HttpResponse::newFileResponse(indexPath.string());
            callback(response);
        },
        {drogon::Get});

    app.registerHandler(
        "/api/mission/alpha-0426",
        [](const drogon::HttpRequestPtr &,
           std::function<void(const drogon::HttpResponsePtr &)> &&callback) {
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

    LOG_WARN << "AeroSentinel Control Center listening on http://0.0.0.0:" << port
             << " with document root " << publicDir.string();
    app.run();
}

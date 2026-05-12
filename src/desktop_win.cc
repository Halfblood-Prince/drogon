#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <winsock2.h>
#include <windows.h>
#include <objbase.h>
#include <unknwn.h>
#include <ws2tcpip.h>

#include <WebView2.h>
#include <wrl.h>

#include <shellapi.h>
#include <shlobj.h>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <string>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace
{
constexpr wchar_t kWindowClass[] = L"AeroSentinelDesktopWindow";
constexpr wchar_t kWindowTitle[] = L"AeroSentinel Control Center";

HWND g_mainWindow = nullptr;
ComPtr<ICoreWebView2Controller> g_webviewController;
ComPtr<ICoreWebView2> g_webview;
PROCESS_INFORMATION g_serverProcess{};
uint16_t g_serverPort = 0;

std::wstring quote(const std::wstring &value)
{
    return L"\"" + value + L"\"";
}

std::filesystem::path moduleDirectory()
{
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD length = 0;

    for (;;)
    {
        length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0)
        {
            return std::filesystem::current_path();
        }

        if (length < buffer.size() - 1)
        {
            buffer.resize(length);
            return std::filesystem::path(buffer).parent_path();
        }

        buffer.resize(buffer.size() * 2);
    }
}

std::filesystem::path localAppDataPath()
{
    PWSTR rawPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &rawPath)))
    {
        std::filesystem::path path(rawPath);
        CoTaskMemFree(rawPath);
        return path;
    }

    return moduleDirectory();
}

uint16_t reserveLocalPort()
{
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
    {
        return 18080;
    }

    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET)
    {
        WSACleanup();
        return 18080;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;

    uint16_t port = 18080;
    if (bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0)
    {
        int length = sizeof(address);
        if (getsockname(listener, reinterpret_cast<sockaddr *>(&address), &length) == 0)
        {
            port = ntohs(address.sin_port);
        }
    }

    closesocket(listener);
    WSACleanup();
    return port;
}

bool startLocalServer()
{
    const auto appDir = moduleDirectory();
    const auto serverPath = appDir / L"aerosentinel-control.exe";

    if (!std::filesystem::exists(serverPath))
    {
        MessageBoxW(g_mainWindow,
                    L"Cannot find aerosentinel-control.exe next to the desktop app.",
                    kWindowTitle,
                    MB_ICONERROR | MB_OK);
        return false;
    }

    g_serverPort = reserveLocalPort();
    SetEnvironmentVariableW(L"PORT", std::to_wstring(g_serverPort).c_str());
    SetEnvironmentVariableW(L"AEROSENTINEL_BIND_ADDRESS", L"127.0.0.1");

    std::wstring commandLine = quote(serverPath.wstring());
    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);

    return CreateProcessW(serverPath.c_str(),
                          commandLine.data(),
                          nullptr,
                          nullptr,
                          FALSE,
                          CREATE_NO_WINDOW,
                          nullptr,
                          appDir.c_str(),
                          &startupInfo,
                          &g_serverProcess) == TRUE;
}

void stopLocalServer()
{
    if (g_serverProcess.hProcess != nullptr)
    {
        TerminateProcess(g_serverProcess.hProcess, 0);
        WaitForSingleObject(g_serverProcess.hProcess, 2500);
        CloseHandle(g_serverProcess.hProcess);
        CloseHandle(g_serverProcess.hThread);
        g_serverProcess = {};
    }
}

bool canConnectToLocalServer()
{
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
    {
        return false;
    }

    SOCKET probe = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (probe == INVALID_SOCKET)
    {
        WSACleanup();
        return false;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(g_serverPort);

    const bool connected = connect(probe,
                                   reinterpret_cast<sockaddr *>(&address),
                                   sizeof(address)) == 0;
    closesocket(probe);
    WSACleanup();
    return connected;
}

bool waitForLocalServer()
{
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        if (canConnectToLocalServer())
        {
            return true;
        }
        Sleep(50);
    }

    return false;
}

void resizeWebView()
{
    if (!g_webviewController)
    {
        return;
    }

    RECT bounds{};
    GetClientRect(g_mainWindow, &bounds);
    g_webviewController->put_Bounds(bounds);
}

std::wstring dashboardUrl()
{
    std::wstringstream url;
    url << L"http://127.0.0.1:" << g_serverPort << L"/mission/alpha-0426";
    return url.str();
}

void initializeWebView()
{
    const auto userDataPath = localAppDataPath() / L"AeroSentinel" / L"WebView2";
    std::error_code cleanupError;
    std::filesystem::remove_all(userDataPath / L"Default" / L"Cookies", cleanupError);
    cleanupError.clear();
    std::filesystem::remove_all(userDataPath / L"Default" / L"Network", cleanupError);
    const auto userData = userDataPath.wstring();

    HRESULT result = CreateCoreWebView2EnvironmentWithOptions(
        nullptr,
        userData.c_str(),
        nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [](HRESULT environmentResult, ICoreWebView2Environment *environment) -> HRESULT {
                if (FAILED(environmentResult) || environment == nullptr)
                {
                    MessageBoxW(g_mainWindow,
                                L"Microsoft Edge WebView2 Runtime is required to run AeroSentinel.",
                                kWindowTitle,
                                MB_ICONERROR | MB_OK);
                    PostQuitMessage(1);
                    return S_OK;
                }

                environment->CreateCoreWebView2Controller(
                    g_mainWindow,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [](HRESULT controllerResult, ICoreWebView2Controller *controller) -> HRESULT {
                            if (FAILED(controllerResult) || controller == nullptr)
                            {
                                MessageBoxW(g_mainWindow,
                                            L"Could not create the AeroSentinel desktop view.",
                                            kWindowTitle,
                                            MB_ICONERROR | MB_OK);
                                PostQuitMessage(1);
                                return S_OK;
                            }

                            g_webviewController = controller;
                            g_webviewController->get_CoreWebView2(&g_webview);
                            resizeWebView();

                            ComPtr<ICoreWebView2Settings> settings;
                            if (SUCCEEDED(g_webview->get_Settings(&settings)) && settings)
                            {
                                settings->put_AreDefaultScriptDialogsEnabled(TRUE);
                                settings->put_IsStatusBarEnabled(FALSE);
                            }

                            g_webview->Navigate(dashboardUrl().c_str());
                            return S_OK;
                        })
                        .Get());

                return S_OK;
            })
            .Get());

    if (FAILED(result))
    {
        MessageBoxW(g_mainWindow,
                    L"Microsoft Edge WebView2 Runtime is required to run AeroSentinel.",
                    kWindowTitle,
                    MB_ICONERROR | MB_OK);
        PostQuitMessage(1);
    }
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
    case WM_SIZE:
        resizeWebView();
        return 0;
    case WM_DESTROY:
        g_webview.Reset();
        g_webviewController.Reset();
        stopLocalServer();
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
    {
        MessageBoxW(nullptr, L"Could not initialize COM.", kWindowTitle, MB_ICONERROR | MB_OK);
        return 1;
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = windowProc;
    windowClass.lpszClassName = kWindowClass;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    RegisterClassExW(&windowClass);

    g_mainWindow = CreateWindowExW(0,
                                  kWindowClass,
                                  kWindowTitle,
                                  WS_OVERLAPPEDWINDOW,
                                  CW_USEDEFAULT,
                                  CW_USEDEFAULT,
                                  1440,
                                  900,
                                  nullptr,
                                  nullptr,
                                  instance,
                                  nullptr);

    if (g_mainWindow == nullptr)
    {
        CoUninitialize();
        return 1;
    }

    ShowWindow(g_mainWindow, showCommand);
    UpdateWindow(g_mainWindow);

    if (!startLocalServer())
    {
        CoUninitialize();
        return 1;
    }

    if (!waitForLocalServer())
    {
        MessageBoxW(g_mainWindow,
                    L"The local AeroSentinel server did not start in time.",
                    kWindowTitle,
                    MB_ICONERROR | MB_OK);
        stopLocalServer();
        CoUninitialize();
        return 1;
    }

    initializeWebView();

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    CoUninitialize();
    return static_cast<int>(message.wParam);
}

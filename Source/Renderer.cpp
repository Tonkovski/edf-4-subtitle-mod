#include "Renderer.h"

#include "Log.h"

#include <detours.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

Renderer* Renderer::instance_ = nullptr;
Renderer::PresentFunction Renderer::originalPresent_ = nullptr;
Renderer::ResizeBuffersFunction Renderer::originalResizeBuffers_ = nullptr;

namespace {

constexpr wchar_t DummyWindowClass[] = L"EDF41SubtitleHookProbe";

ImU32 ColorWithAlpha(std::uint32_t rgb, float alpha) {
    const auto red = static_cast<int>((rgb >> 16U) & 0xFFU);
    const auto green = static_cast<int>((rgb >> 8U) & 0xFFU);
    const auto blue = static_cast<int>(rgb & 0xFFU);
    const auto alphaByte =
        static_cast<int>(std::lround(std::clamp(alpha, 0.0F, 1.0F) * 255.0F));
    return IM_COL32(red, green, blue, alphaByte);
}

std::filesystem::path WindowsFontPath(std::wstring_view filename) {
    std::array<wchar_t, MAX_PATH> windowsDirectory{};
    const UINT length =
        GetWindowsDirectoryW(windowsDirectory.data(), static_cast<UINT>(windowsDirectory.size()));
    if (length == 0 || length >= windowsDirectory.size()) {
        return {};
    }
    return std::filesystem::path(windowsDirectory.data()) / L"Fonts" / filename;
}

} // namespace

bool Renderer::InstallHooks(
    const ModConfig& config,
    const SubtitleDatabase& database,
    SubtitleController& controller) {
    if (hooksInstalled_) {
        return false;
    }

    config_ = &config;
    database_ = &database;
    controller_ = &controller;
    instance_ = this;

    if (!FindHookTargets()) {
        logging::Error("Could not resolve the DX11 Present and ResizeBuffers functions.");
        instance_ = nullptr;
        return false;
    }

    LONG result = DetourTransactionBegin();
    if (result != NO_ERROR) {
        logging::Error("DetourTransactionBegin failed for DX11 hooks: " + std::to_string(result));
        instance_ = nullptr;
        return false;
    }

    DetourUpdateThread(GetCurrentThread());
    result = DetourAttach(
        reinterpret_cast<PVOID*>(&originalPresent_),
        reinterpret_cast<PVOID>(PresentHook));
    if (result == NO_ERROR) {
        result = DetourAttach(
            reinterpret_cast<PVOID*>(&originalResizeBuffers_),
            reinterpret_cast<PVOID>(ResizeBuffersHook));
    }

    if (result != NO_ERROR) {
        DetourTransactionAbort();
        logging::Error("DetourAttach failed for DX11 hooks: " + std::to_string(result));
        instance_ = nullptr;
        return false;
    }

    result = DetourTransactionCommit();
    if (result != NO_ERROR) {
        logging::Error("DetourTransactionCommit failed for DX11 hooks: " + std::to_string(result));
        instance_ = nullptr;
        return false;
    }

    hooksInstalled_ = true;
    logging::Info("DX11 Present and ResizeBuffers hooks installed.");
    return true;
}

void Renderer::RemoveHooks() {
    if (!hooksInstalled_) {
        return;
    }

    if (DetourTransactionBegin() == NO_ERROR) {
        DetourUpdateThread(GetCurrentThread());
        DetourDetach(
            reinterpret_cast<PVOID*>(&originalPresent_),
            reinterpret_cast<PVOID>(PresentHook));
        DetourDetach(
            reinterpret_cast<PVOID*>(&originalResizeBuffers_),
            reinterpret_cast<PVOID>(ResizeBuffersHook));
        DetourTransactionCommit();
    }

    {
        std::scoped_lock lock(renderMutex_);
        ShutdownGraphics();
    }

    hooksInstalled_ = false;
    instance_ = nullptr;
}

HRESULT WINAPI Renderer::PresentHook(
    IDXGISwapChain* swapChain,
    UINT syncInterval,
    UINT flags) {
    if (instance_ != nullptr && !instance_->renderFailed_) {
        try {
            instance_->Render(swapChain);
        } catch (const std::exception& exception) {
            instance_->renderFailed_ = true;
            logging::Error(std::string("Rendering disabled after exception: ") + exception.what());
        } catch (...) {
            instance_->renderFailed_ = true;
            logging::Error("Rendering disabled after an unknown exception.");
        }
    }
    return originalPresent_(swapChain, syncInterval, flags);
}

HRESULT WINAPI Renderer::ResizeBuffersHook(
    IDXGISwapChain* swapChain,
    UINT bufferCount,
    UINT width,
    UINT height,
    DXGI_FORMAT format,
    UINT flags) {
    if (instance_ != nullptr) {
        instance_->BeforeResize(swapChain);
    }
    const HRESULT result =
        originalResizeBuffers_(swapChain, bufferCount, width, height, format, flags);
    if (instance_ != nullptr) {
        instance_->AfterResize(swapChain, result);
    }
    return result;
}

bool Renderer::FindHookTargets() {
    const HINSTANCE instance = GetModuleHandleW(nullptr);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = DummyWindowClass;
    RegisterClassExW(&windowClass);

    const HWND window = CreateWindowExW(
        0,
        DummyWindowClass,
        L"",
        WS_OVERLAPPEDWINDOW,
        0,
        0,
        100,
        100,
        nullptr,
        nullptr,
        instance,
        nullptr);
    if (window == nullptr) {
        logging::Error("Failed to create the temporary DX11 probe window.");
        return false;
    }

    DXGI_SWAP_CHAIN_DESC description{};
    description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = 1;
    description.OutputWindow = window;
    description.Windowed = TRUE;
    description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    constexpr std::array featureLevels{
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    IDXGISwapChain* probeSwapChain = nullptr;
    ID3D11Device* probeDevice = nullptr;
    D3D_FEATURE_LEVEL selectedFeatureLevel{};
    HRESULT result = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        featureLevels.data(),
        static_cast<UINT>(featureLevels.size()),
        D3D11_SDK_VERSION,
        &description,
        &probeSwapChain,
        &probeDevice,
        &selectedFeatureLevel,
        nullptr);

    if (FAILED(result)) {
        result = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            0,
            featureLevels.data(),
            static_cast<UINT>(featureLevels.size()),
            D3D11_SDK_VERSION,
            &description,
            &probeSwapChain,
            &probeDevice,
            &selectedFeatureLevel,
            nullptr);
    }

    if (SUCCEEDED(result) && probeSwapChain != nullptr) {
        auto** vtable = *reinterpret_cast<void***>(probeSwapChain);
        originalPresent_ = reinterpret_cast<PresentFunction>(vtable[8]);
        originalResizeBuffers_ = reinterpret_cast<ResizeBuffersFunction>(vtable[13]);
    }

    if (probeSwapChain != nullptr) {
        probeSwapChain->Release();
    }
    if (probeDevice != nullptr) {
        probeDevice->Release();
    }
    DestroyWindow(window);
    UnregisterClassW(DummyWindowClass, instance);

    return SUCCEEDED(result) && originalPresent_ != nullptr && originalResizeBuffers_ != nullptr;
}

bool Renderer::InitializeSwapChain(IDXGISwapChain* swapChain) {
    DXGI_SWAP_CHAIN_DESC description{};
    if (!IsCandidateSwapChain(swapChain, description)) {
        return false;
    }

    if (targetSwapChain_ != nullptr && targetSwapChain_ != swapChain) {
        if (description.OutputWindow != renderWindow_ && IsWindow(renderWindow_) != FALSE) {
            return false;
        }
        ShutdownGraphics();
    }

    if (targetSwapChain_ == swapChain && device_ != nullptr && renderTarget_ != nullptr) {
        return true;
    }

    HRESULT result =
        swapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device_));
    if (FAILED(result) || device_ == nullptr) {
        logging::Error("IDXGISwapChain::GetDevice failed.");
        return false;
    }

    device_->GetImmediateContext(&context_);
    if (context_ == nullptr) {
        device_->Release();
        device_ = nullptr;
        logging::Error("ID3D11Device::GetImmediateContext failed.");
        return false;
    }

    targetSwapChain_ = swapChain;
    renderWindow_ = description.OutputWindow;

    if (!CreateRenderTarget() || !InitializeImGui()) {
        ShutdownGraphics();
        return false;
    }

    logging::Info("Subtitle renderer attached to the EDF41 swap chain.");
    return true;
}

bool Renderer::InitializeImGui() {
    IMGUI_CHECKVERSION();
    imguiContext_ = ImGui::CreateContext();
    if (imguiContext_ == nullptr) {
        return false;
    }

    ImGui::SetCurrentContext(imguiContext_);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    ImFontGlyphRangesBuilder glyphBuilder;
    glyphBuilder.AddRanges(io.Fonts->GetGlyphRangesDefault());
    for (const auto& text : database_->GlyphSource()) {
        glyphBuilder.AddText(text.c_str());
    }
    glyphBuilder.BuildRanges(&glyphRanges_);

    const std::filesystem::path fontPath = SelectFont();
    if (!fontPath.empty()) {
        ImFontConfig fontConfiguration{};
        fontConfiguration.FontNo = 0;
        fontConfiguration.OversampleH = 2;
        fontConfiguration.OversampleV = 1;
        fontConfiguration.PixelSnapH = false;
        fontConfiguration.GlyphRanges = glyphRanges_.Data;

        const std::string fontPathUtf8 = logging::PathToUtf8(fontPath);
        font_ = io.Fonts->AddFontFromFileTTF(
            fontPathUtf8.c_str(),
            config_->fontSize,
            &fontConfiguration,
            glyphRanges_.Data);
        if (font_ != nullptr) {
            logging::Info("Using subtitle font " + fontPathUtf8 + ".");
        }
    }

    if (font_ == nullptr) {
        font_ = io.Fonts->AddFontDefault();
        logging::Warning("No suitable configured/system font was found; using ImGui's Latin fallback.");
    }
    if (!io.Fonts->Build()) {
        logging::Error("Failed to build the ImGui font atlas.");
        return false;
    }

    ImGuiStyle& style = ImGui::GetStyle();
    style.AntiAliasedLines = true;
    style.AntiAliasedFill = true;

    win32BackendInitialized_ = ImGui_ImplWin32_Init(renderWindow_);
    if (!win32BackendInitialized_) {
        logging::Error("ImGui Win32 backend initialization failed.");
        return false;
    }

    dx11BackendInitialized_ = ImGui_ImplDX11_Init(device_, context_);
    if (!dx11BackendInitialized_) {
        logging::Error("ImGui DX11 backend initialization failed.");
        return false;
    }
    return true;
}

bool Renderer::CreateRenderTarget() {
    if (targetSwapChain_ == nullptr || device_ == nullptr) {
        return false;
    }

    ID3D11Texture2D* backBuffer = nullptr;
    HRESULT result = targetSwapChain_->GetBuffer(
        0,
        __uuidof(ID3D11Texture2D),
        reinterpret_cast<void**>(&backBuffer));
    if (FAILED(result) || backBuffer == nullptr) {
        return false;
    }

    result = device_->CreateRenderTargetView(backBuffer, nullptr, &renderTarget_);
    backBuffer->Release();
    return SUCCEEDED(result) && renderTarget_ != nullptr;
}

bool Renderer::IsCandidateSwapChain(
    IDXGISwapChain* swapChain,
    DXGI_SWAP_CHAIN_DESC& description) const {
    if (swapChain == nullptr || FAILED(swapChain->GetDesc(&description)) ||
        description.OutputWindow == nullptr) {
        return false;
    }

    DWORD processId = 0;
    GetWindowThreadProcessId(description.OutputWindow, &processId);
    if (processId != GetCurrentProcessId()) {
        return false;
    }

    RECT clientRectangle{};
    if (GetClientRect(description.OutputWindow, &clientRectangle) == FALSE) {
        return false;
    }
    const LONG width = clientRectangle.right - clientRectangle.left;
    const LONG height = clientRectangle.bottom - clientRectangle.top;
    return width >= 640 && height >= 360;
}

void Renderer::Render(IDXGISwapChain* swapChain) {
    std::scoped_lock lock(renderMutex_);
    if (!InitializeSwapChain(swapChain) || renderTarget_ == nullptr) {
        return;
    }

    const auto subtitles = controller_->Update(std::chrono::steady_clock::now());
    if (subtitles.empty()) {
        return;
    }

    ImGui::SetCurrentContext(imguiContext_);
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    DrawSubtitles(subtitles);

    ImGui::Render();
    context_->OMSetRenderTargets(1, &renderTarget_, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void Renderer::BeforeResize(IDXGISwapChain* swapChain) {
    std::scoped_lock lock(renderMutex_);
    if (swapChain != targetSwapChain_) {
        return;
    }

    if (context_ != nullptr) {
        context_->OMSetRenderTargets(0, nullptr, nullptr);
        context_->Flush();
    }
    ReleaseRenderTarget();
}

void Renderer::AfterResize(IDXGISwapChain* swapChain, HRESULT result) {
    std::scoped_lock lock(renderMutex_);
    if (swapChain != targetSwapChain_) {
        return;
    }

    if (SUCCEEDED(result)) {
        if (!CreateRenderTarget()) {
            logging::Error("Failed to recreate the render target after ResizeBuffers.");
        }
    } else {
        logging::Warning("The game's ResizeBuffers call failed.");
    }
}

void Renderer::ReleaseRenderTarget() {
    if (renderTarget_ != nullptr) {
        renderTarget_->Release();
        renderTarget_ = nullptr;
    }
}

void Renderer::ShutdownGraphics() {
    if (imguiContext_ != nullptr) {
        ImGui::SetCurrentContext(imguiContext_);
        if (dx11BackendInitialized_) {
            ImGui_ImplDX11_Shutdown();
        }
        if (win32BackendInitialized_) {
            ImGui_ImplWin32_Shutdown();
        }
        ImGui::DestroyContext(imguiContext_);
    }

    dx11BackendInitialized_ = false;
    win32BackendInitialized_ = false;
    imguiContext_ = nullptr;
    font_ = nullptr;
    glyphRanges_.clear();

    ReleaseRenderTarget();
    if (context_ != nullptr) {
        context_->Release();
        context_ = nullptr;
    }
    if (device_ != nullptr) {
        device_->Release();
        device_ = nullptr;
    }

    targetSwapChain_ = nullptr;
    renderWindow_ = nullptr;
}

std::filesystem::path Renderer::SelectFont() const {
    std::error_code error;
    if (!config_->fontFile.empty() && std::filesystem::is_regular_file(config_->fontFile, error)) {
        return config_->fontFile;
    }

    constexpr std::array candidates{
        std::wstring_view(L"msjh.ttc"),
        std::wstring_view(L"msyh.ttc"),
        std::wstring_view(L"segoeui.ttf"),
    };
    for (const auto candidate : candidates) {
        auto path = WindowsFontPath(candidate);
        error.clear();
        if (!path.empty() && std::filesystem::is_regular_file(path, error)) {
            return path;
        }
    }
    return {};
}

void Renderer::DrawSubtitles(const std::vector<RenderableSubtitle>& subtitles) {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.DisplaySize.x <= 0.0F || io.DisplaySize.y <= 0.0F) {
        return;
    }

    const float boxWidth = std::min(
        config_->maximumWidth,
        std::max(240.0F, io.DisplaySize.x - 80.0F));
    const float wrapWidth = std::max(100.0F, boxWidth - config_->paddingX * 2.0F);

    std::vector<ImVec2> textSizes;
    textSizes.reserve(subtitles.size());
    float contentHeight = 0.0F;
    for (const auto& subtitle : subtitles) {
        const ImVec2 size = font_->CalcTextSizeA(
            config_->fontSize,
            std::numeric_limits<float>::max(),
            wrapWidth,
            subtitle.text.c_str());
        textSizes.push_back(size);
        contentHeight += size.y;
    }
    if (subtitles.size() > 1) {
        contentHeight += config_->lineGap * static_cast<float>(subtitles.size() - 1);
    }

    const float boxHeight = contentHeight + config_->paddingY * 2.0F;
    const float left = (io.DisplaySize.x - boxWidth) * 0.5F;
    const float top = std::max(20.0F, io.DisplaySize.y - config_->marginBottom - boxHeight);

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    drawList->AddRectFilled(
        ImVec2(left, top),
        ImVec2(left + boxWidth, top + boxHeight),
        ColorWithAlpha(config_->backgroundColor, config_->backgroundAlpha),
        4.0F);

    float currentY = top + config_->paddingY;
    for (std::size_t index = 0; index < subtitles.size(); ++index) {
        const auto& subtitle = subtitles[index];
        const auto& textSize = textSizes[index];
        const float textX =
            left + config_->paddingX + std::max(0.0F, (wrapWidth - textSize.x) * 0.5F);
        const std::uint32_t rgb =
            subtitle.kind == VoiceKind::Event ? config_->eventColor : config_->ambientColor;

        drawList->AddText(
            font_,
            config_->fontSize,
            ImVec2(textX, currentY),
            ColorWithAlpha(rgb, subtitle.opacity),
            subtitle.text.c_str(),
            nullptr,
            wrapWidth);
        currentY += textSize.y + config_->lineGap;
    }
}


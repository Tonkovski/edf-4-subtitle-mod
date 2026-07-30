#pragma once

#include "Config.h"
#include "SubtitleController.h"
#include "SubtitleDatabase.h"

#include <d3d11.h>
#include <dxgi.h>
#include <imgui.h>
#include <windows.h>

#include <mutex>

class Renderer {
public:
    bool InstallHooks(
        const ModConfig& config,
        const SubtitleDatabase& database,
        SubtitleController& controller);
    void RemoveHooks();

private:
    using PresentFunction = HRESULT(WINAPI*)(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags);
    using ResizeBuffersFunction = HRESULT(WINAPI*)(
        IDXGISwapChain* swapChain,
        UINT bufferCount,
        UINT width,
        UINT height,
        DXGI_FORMAT format,
        UINT flags);

    static HRESULT WINAPI PresentHook(
        IDXGISwapChain* swapChain,
        UINT syncInterval,
        UINT flags);
    static HRESULT WINAPI ResizeBuffersHook(
        IDXGISwapChain* swapChain,
        UINT bufferCount,
        UINT width,
        UINT height,
        DXGI_FORMAT format,
        UINT flags);

    bool FindHookTargets();
    bool InitializeSwapChain(IDXGISwapChain* swapChain);
    bool InitializeImGui();
    bool CreateRenderTarget();
    bool IsCandidateSwapChain(IDXGISwapChain* swapChain, DXGI_SWAP_CHAIN_DESC& description) const;
    void Render(IDXGISwapChain* swapChain);
    void BeforeResize(IDXGISwapChain* swapChain);
    void AfterResize(IDXGISwapChain* swapChain, HRESULT result);
    void ReleaseRenderTarget();
    void ShutdownGraphics();
    std::filesystem::path SelectFont() const;
    void DrawSubtitles(const std::vector<RenderableSubtitle>& subtitles);

    static Renderer* instance_;
    static PresentFunction originalPresent_;
    static ResizeBuffersFunction originalResizeBuffers_;

    const ModConfig* config_ = nullptr;
    const SubtitleDatabase* database_ = nullptr;
    SubtitleController* controller_ = nullptr;

    std::mutex renderMutex_;
    IDXGISwapChain* targetSwapChain_ = nullptr;
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    ID3D11RenderTargetView* renderTarget_ = nullptr;
    HWND renderWindow_ = nullptr;

    ImGuiContext* imguiContext_ = nullptr;
    ImFont* font_ = nullptr;
    ImVector<ImWchar> glyphRanges_;
    bool win32BackendInitialized_ = false;
    bool dx11BackendInitialized_ = false;
    bool hooksInstalled_ = false;
    bool renderFailed_ = false;
};


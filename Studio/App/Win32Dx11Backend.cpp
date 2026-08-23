// Reverie/Studio/App/Win32Dx11Backend.cpp - the initial concrete Studio backend (Win32 + DX11).
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// This is the ONLY translation unit that knows about Win32, DirectX 11, or the Dear ImGui platform/
// renderer backends. It implements the portable IStudioBackend, so a Linux/macOS backend can be
// added later purely by writing a sibling file + CreateStudioBackend() without touching the editor.
#if defined(_WIN32)

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include "StudioBackend.h"

#include "imgui.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"

#include <d3d11.h>
#include <tchar.h>
#include <windows.h>

// Forward-declared in the ImGui Win32 backend.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam,
                                                             LPARAM lParam);

namespace reverie::studio {

namespace {

class Win32Dx11Backend final : public IStudioBackend {
public:
    bool Init(const BackendConfig& cfg);
    ~Win32Dx11Backend() override;

    bool PumpEvents() override;
    void BeginFrame() override;
    void EndFrame(const float clearColor[4]) override;
    const char* Name() const override { return "Win32 + DirectX 11"; }

    void OnResize(UINT w, UINT h) { resizeW_ = w; resizeH_ = h; }

private:
    bool CreateDeviceD3D();
    void CleanupDeviceD3D();
    void CreateRenderTarget();
    void CleanupRenderTarget();

    HWND hwnd_ = nullptr;
    WNDCLASSEXW wc_{};
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    IDXGISwapChain* swapChain_ = nullptr;
    ID3D11RenderTargetView* rtv_ = nullptr;
    UINT resizeW_ = 0, resizeH_ = 0;
    bool quit_ = false;
};

Win32Dx11Backend* g_backend = nullptr; // for the WndProc to reach the instance (single window)

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    switch (msg) {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED && g_backend != nullptr)
            g_backend->OnResize(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_KEYMENU) return 0; // disable ALT application menu
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

bool Win32Dx11Backend::CreateDeviceD3D() {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd_;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT flags = 0;
    D3D_FEATURE_LEVEL level;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                               levels, 2, D3D11_SDK_VERSION, &sd, &swapChain_,
                                               &device_, &level, &context_);
    if (hr == DXGI_ERROR_UNSUPPORTED) // fall back to WARP software device
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, 2,
                                           D3D11_SDK_VERSION, &sd, &swapChain_, &device_, &level,
                                           &context_);
    if (FAILED(hr)) return false;
    CreateRenderTarget();
    return true;
}

void Win32Dx11Backend::CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (swapChain_) { swapChain_->Release(); swapChain_ = nullptr; }
    if (context_) { context_->Release(); context_ = nullptr; }
    if (device_) { device_->Release(); device_ = nullptr; }
}

void Win32Dx11Backend::CreateRenderTarget() {
    ID3D11Texture2D* backBuffer = nullptr;
    swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (backBuffer) {
        device_->CreateRenderTargetView(backBuffer, nullptr, &rtv_);
        backBuffer->Release();
    }
}

void Win32Dx11Backend::CleanupRenderTarget() {
    if (rtv_) { rtv_->Release(); rtv_ = nullptr; }
}

bool Win32Dx11Backend::Init(const BackendConfig& cfg) {
    g_backend = this;
    wc_ = {sizeof(wc_), CS_CLASSDC, WndProc, 0L, 0L, ::GetModuleHandleW(nullptr), nullptr, nullptr,
           nullptr, nullptr, L"ReverieStudioWindow", nullptr};
    ::RegisterClassExW(&wc_);
    hwnd_ = ::CreateWindowExW(0, wc_.lpszClassName, L"Reverie Studio", WS_OVERLAPPEDWINDOW, 100, 100,
                             cfg.width, cfg.height, nullptr, nullptr, wc_.hInstance, nullptr);
    if (hwnd_ == nullptr) { ::UnregisterClassW(wc_.lpszClassName, wc_.hInstance); return false; }

    if (!CreateDeviceD3D()) {
        CleanupDeviceD3D();
        ::DestroyWindow(hwnd_);
        ::UnregisterClassW(wc_.lpszClassName, wc_.hInstance);
        return false;
    }
    ::ShowWindow(hwnd_, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd_);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    if (cfg.enableDocking) io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    if (cfg.enableViewports) io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.IniFilename = "reverie_studio_layout.ini"; // persisted dock layout / window positions

    ImGui_ImplWin32_Init(hwnd_);
    ImGui_ImplDX11_Init(device_, context_);
    return true;
}

Win32Dx11Backend::~Win32Dx11Backend() {
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    if (hwnd_) ::DestroyWindow(hwnd_);
    ::UnregisterClassW(wc_.lpszClassName, wc_.hInstance);
    g_backend = nullptr;
}

bool Win32Dx11Backend::PumpEvents() {
    MSG msg;
    while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
        ::TranslateMessage(&msg);
        ::DispatchMessage(&msg);
        if (msg.message == WM_QUIT) quit_ = true;
    }
    if (quit_) return false;
    if (resizeW_ != 0 && resizeH_ != 0) {
        CleanupRenderTarget();
        swapChain_->ResizeBuffers(0, resizeW_, resizeH_, DXGI_FORMAT_UNKNOWN, 0);
        resizeW_ = resizeH_ = 0;
        CreateRenderTarget();
    }
    return true;
}

void Win32Dx11Backend::BeginFrame() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void Win32Dx11Backend::EndFrame(const float clearColor[4]) {
    ImGui::Render();
    context_->OMSetRenderTargets(1, &rtv_, nullptr);
    context_->ClearRenderTargetView(rtv_, clearColor);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
    swapChain_->Present(1, 0); // vsync
}

} // namespace

std::unique_ptr<IStudioBackend> CreateStudioBackend(const BackendConfig& config) {
    auto backend = std::make_unique<Win32Dx11Backend>();
    if (!backend->Init(config)) return nullptr;
    return backend;
}

} // namespace reverie::studio

#else // !_WIN32

#include "StudioBackend.h"
namespace reverie::studio {
std::unique_ptr<IStudioBackend> CreateStudioBackend(const BackendConfig&) {
    return nullptr; // no GUI backend on this platform yet (Linux/macOS backends go here)
}
} // namespace reverie::studio

#endif

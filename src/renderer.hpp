#pragma once

#include <wrl.h>
#include <dxgi1_6.h>
#include <directx/d3d12.h>
#include <directx/d3dx12.h>
#include <SDL.h>

using Microsoft::WRL::ComPtr;

namespace Renderer
{
    constexpr D3D_FEATURE_LEVEL MinFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    constexpr DXGI_FORMAT SwapColorFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
    constexpr DXGI_FORMAT SwapColorSRGBFormat = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    constexpr DXGI_FORMAT SwapDepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    constexpr uint32_t FrameCount = 3;

    inline ComPtr<IDXGIFactory6> dxgiFactory = nullptr;
    inline ComPtr<IDXGIAdapter1> dxgiAdapter = nullptr;
    inline ComPtr<ID3D12Device> device = nullptr;

    inline ComPtr<ID3D12CommandQueue> commandQueue = nullptr;
    inline uint32_t rtvHeapIncrementSize = 0;
    inline uint32_t dsvHeapIncrementSize = 0;
    inline uint32_t cbvsrvHeapIncrementSize = 0;

    inline BOOL tearingSupport = FALSE;
    inline ComPtr<IDXGISwapChain4> swapchain = nullptr;
    inline ComPtr<ID3D12Resource> renderTargets[FrameCount]{};
    inline ComPtr<ID3D12Resource> depthStencilTarget = nullptr;
    inline ComPtr<ID3D12DescriptorHeap> rtvHeap = nullptr; //< for swap rtvs
    inline ComPtr<ID3D12DescriptorHeap> dsvHeap = nullptr; //< for swap dsv(s)

    inline ComPtr<ID3D12CommandAllocator> commandAllocator = nullptr;
    inline ComPtr<ID3D12GraphicsCommandList> commandList = nullptr;
    inline ComPtr<ID3D12Fence> fence = nullptr;
    inline HANDLE fenceEvent = nullptr;
    inline uint64_t fenceValue = 0;

    bool init(SDL_Window* pWindow);

    void shutdown();

    bool resizeSwapResources(uint32_t width, uint32_t height);

    void waitForGPU();
} // namespace Renderer

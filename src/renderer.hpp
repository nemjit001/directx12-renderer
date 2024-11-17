#pragma once

#include <wrl.h>
#include <dxgi1_6.h>
#include <directx/d3d12.h>
#include <directx/d3dx12.h>
#include <SDL.h>

using Microsoft::WRL::ComPtr;

struct Buffer
{
    void destroy();

    void map();

    void unmap();

    ComPtr<ID3D12Resource> handle;
    size_t size;
    bool mapped;
    void* pData;
};

struct Texture
{
    void destroy();

    ComPtr<ID3D12Resource> handle;
    DXGI_FORMAT format;
    uint32_t width;
    uint32_t height;
    uint32_t depthOrLayers;
    uint32_t levels;
};

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
    inline Texture depthStencilTarget{};
    inline ComPtr<ID3D12DescriptorHeap> rtvHeap = nullptr; //< for swap rtvs
    inline ComPtr<ID3D12DescriptorHeap> dsvHeap = nullptr; //< for swap dsv(s)

    inline ComPtr<ID3D12Fence> fence = nullptr;
    inline HANDLE fenceEvent = nullptr;
    inline uint64_t fenceValue = 0;

    inline ComPtr<ID3D12CommandAllocator> commandAllocator = nullptr;
    inline ComPtr<ID3D12GraphicsCommandList> commandList = nullptr;

    bool init(SDL_Window* pWindow);

    void shutdown();

    bool resizeSwapResources(uint32_t width, uint32_t height);

    bool createBuffer(
        Buffer& buffer,
        size_t size,
        D3D12_RESOURCE_STATES resourceState,
        D3D12_HEAP_TYPE heap,
        bool createMapped = false
    );

    bool createTexture(
        Texture& texture,
        D3D12_RESOURCE_DIMENSION dimension,
        DXGI_FORMAT format,
        D3D12_RESOURCE_FLAGS flags,
        D3D12_RESOURCE_STATES resourceState,
        D3D12_HEAP_TYPE heap,
        uint32_t width,
        uint32_t height,
        uint32_t depth,
        uint32_t levels = 1,
        uint32_t layers = 1,
        uint32_t samples = 1,
        uint32_t sampleQuality = 0,
        D3D12_CLEAR_VALUE* pOptimizedClearValue = nullptr,
        D3D12_TEXTURE_LAYOUT initialLayout = D3D12_TEXTURE_LAYOUT_UNKNOWN
    );

    void waitForGPU();
} // namespace Renderer

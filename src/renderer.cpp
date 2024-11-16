#include "renderer.hpp"

#include <SDL_syswm.h>

namespace Renderer
{
	bool init(SDL_Window* pWindow)
	{
        // Create DXGI factory
        uint32_t factoryFlags = 0;
#ifndef NDEBUG
        ComPtr<ID3D12Debug1> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
        {
            factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
            debug->EnableDebugLayer();
            debug->SetEnableGPUBasedValidation(TRUE);
            debug->SetEnableSynchronizedCommandQueueValidation(TRUE);
        }
#endif

        if (FAILED(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&dxgiFactory))))
        {
            printf("DXGI factory create failed\n");
            return false;
        }

        // auto select adapter
        {
            ComPtr<IDXGIAdapter1> adapter;
            for (UINT adapterIdx = 0; SUCCEEDED(dxgiFactory->EnumAdapterByGpuPreference(adapterIdx, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter))); adapterIdx++)
            {
                DXGI_ADAPTER_DESC1 desc{};
                adapter->GetDesc1(&desc);

                if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
                    continue;
                }

                if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), MinFeatureLevel, __uuidof(ID3D12Device), nullptr)))
                {
                    dxgiAdapter = adapter;
                    break;
                }
            }

            if (dxgiAdapter == nullptr)
            {
                // No high performance adapter, search for other adapters
                for (UINT adapterIdx = 0; SUCCEEDED(dxgiFactory->EnumAdapterByGpuPreference(adapterIdx, DXGI_GPU_PREFERENCE_UNSPECIFIED, IID_PPV_ARGS(&adapter))); adapterIdx++)
                {
                    DXGI_ADAPTER_DESC1 desc{};
                    adapter->GetDesc1(&desc);

                    if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
                        continue;
                    }

                    if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), MinFeatureLevel, __uuidof(ID3D12Device), nullptr)))
                    {
                        dxgiAdapter = adapter;
                        break;
                    }
                }
            }

            if (dxgiAdapter == nullptr)
            {
                printf("DXGI adapter select failed\n");
                return false;
            }
        }

        // Create device & command queue
        DXGI_ADAPTER_DESC1 adapterDesc{};
        dxgiAdapter->GetDesc1(&adapterDesc);
        printf("Automagically selected adapter: %ls\n", adapterDesc.Description);

        if (FAILED(D3D12CreateDevice(dxgiAdapter.Get(), MinFeatureLevel, IID_PPV_ARGS(&device))))
        {
            printf("D3D12 device create failed\n");
            return false;
        }

        D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
        commandQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        commandQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        commandQueueDesc.NodeMask = 0x00;

        if (FAILED(device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue))))
        {
            printf("D3D12 command queue create failed\n");
            return false;
        }

        // Get descriptor increment sizes
        rtvHeapIncrementSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        dsvHeapIncrementSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        cbvsrvHeapIncrementSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        // Create swap chain
        int windowWidth = 0;
        int windowHeight = 0;
        SDL_GetWindowSize(pWindow, &windowWidth, &windowHeight);

        SDL_SysWMinfo wmInfo{};
        SDL_VERSION(&wmInfo.version);
        if (!SDL_GetWindowWMInfo(pWindow, &wmInfo))
        {
            printf("SDL get HWND handle failed: %s\n", SDL_GetError());
            return false;
        }

        if (FAILED(dxgiFactory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearingSupport, sizeof(tearingSupport))))
        {
            printf("DXGI tearing support check failed\n");
            return false;
        }

        uint32_t swapchainFlags = 0;
        if (tearingSupport == TRUE) {
            swapchainFlags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
        }

        DXGI_SWAP_CHAIN_DESC1 swapchainDesc{};
        swapchainDesc.Flags = swapchainFlags;
        swapchainDesc.Width = windowWidth;
        swapchainDesc.Height = windowHeight;
        swapchainDesc.Format = SwapColorFormat;
        swapchainDesc.Stereo = FALSE;
        swapchainDesc.SampleDesc.Count = 1;
        swapchainDesc.SampleDesc.Quality = 0;
        swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapchainDesc.BufferCount = FrameCount;
        swapchainDesc.Scaling = DXGI_SCALING_STRETCH;
        swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapchainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

        ComPtr<IDXGISwapChain1> swapchain1;
        if (FAILED(dxgiFactory->CreateSwapChainForHwnd(commandQueue.Get(), wmInfo.info.win.window, &swapchainDesc, nullptr, nullptr, &swapchain1))
            || FAILED(dxgiFactory->MakeWindowAssociation(wmInfo.info.win.window, DXGI_MWA_NO_ALT_ENTER))
            || FAILED(swapchain1.As(&swapchain)))
        {
            printf("DXGI swap chain create failed\n");
            return false;
        }

        // Create descriptor heap for swap RTVs
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.NumDescriptors = FrameCount;
        rtvHeapDesc.NodeMask = 0x00;

        if (FAILED(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap))))
        {
            printf("D3D12 rtv heap create failed\n");
            return false;
        }

        // Create descriptor heap for swap DSVs
        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
        dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.NumDescriptors = 1;
        dsvHeapDesc.NodeMask = 0x00;

        if (FAILED(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvHeap))))
        {
            printf("D3D12 dsv heap create failed\n");
            return false;
        }

        // Create frame resources
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvHeap->GetCPUDescriptorHandleForHeapStart());
        for (uint32_t frameIdx = 0; frameIdx < FrameCount; frameIdx++)
        {
            if (FAILED(swapchain->GetBuffer(frameIdx, IID_PPV_ARGS(&renderTargets[frameIdx]))))
            {
                printf("D3D12 get swap buffer failed\n");
                return false;
            }

            D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
            rtvDesc.Format = SwapColorSRGBFormat;
            rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            rtvDesc.Texture2D.MipSlice = 0;
            rtvDesc.Texture2D.PlaneSlice = 0;

            device->CreateRenderTargetView(renderTargets[frameIdx].Get(), &rtvDesc, rtvHandle);
            rtvHandle.Offset(1, rtvHeapIncrementSize);
        }

        D3D12_RESOURCE_DESC depthStencilDesc = CD3DX12_RESOURCE_DESC::Tex2D(SwapDepthStencilFormat, windowWidth, windowHeight, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
        D3D12_CLEAR_VALUE depthStencilClearValue = CD3DX12_CLEAR_VALUE(SwapDepthStencilFormat, 1.0F, 0x00);
        D3D12_HEAP_PROPERTIES depthStencilHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        if (FAILED(device->CreateCommittedResource(&depthStencilHeap, D3D12_HEAP_FLAG_NONE, &depthStencilDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthStencilClearValue, IID_PPV_ARGS(&depthStencilTarget))))
        {
            printf("D3D12 create swap depth buffer failed\n");
            return false;
        }

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
        dsvDesc.Format = SwapDepthStencilFormat;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Texture2D.MipSlice = 0;

        CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(dsvHeap->GetCPUDescriptorHandleForHeapStart());
        device->CreateDepthStencilView(depthStencilTarget.Get(), &dsvDesc, dsvHandle);

        // Create command allocator & command list
        if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator))))
        {
            printf("D3D12 command allocator create failed\n");
            return false;
        }

        if (FAILED(device->CreateCommandList(0x00, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList))))
        {
            printf("D3D12 command list create failed\n");
            return false;
        }
        commandList->Close(); //< close on create, reset happens in render

        // Create synchronization primitives
        fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))
            || fenceEvent == nullptr)
        {
            printf("D3D12 frame fence create failed\n");
            return false;
        }

        return true;
	}

	void shutdown()
	{
        CloseHandle(fenceEvent);
        fence.Reset();
        commandList.Reset();
        commandAllocator.Reset();

        dsvHeap.Reset();
        rtvHeap.Reset();
        depthStencilTarget.Reset();
        for (UINT i = 0; i < FrameCount; i++) {
            renderTargets[i].Reset();
        }
        swapchain.Reset();
        
        commandQueue.Reset();
        device.Reset();
        dxgiAdapter.Reset();
        dxgiFactory.Reset();
	}

    bool resizeSwapResources(uint32_t width, uint32_t height)
    {
        // Release swap resources
        for (uint32_t frameIdx = 0; frameIdx < FrameCount; frameIdx++) {
            renderTargets[frameIdx].Reset();
        }

        // Resize swap buffers
        DXGI_SWAP_CHAIN_DESC1 swapDesc{};
        if (FAILED(swapchain->GetDesc1(&swapDesc))
            || FAILED(swapchain->ResizeBuffers(swapDesc.BufferCount, width, height, swapDesc.Format, swapDesc.Flags)))
        {
            printf("DXGI swap chain resize failed\n");
            return false;
        }

        // Resize depth buffer
        D3D12_RESOURCE_DESC depthStencilDesc = CD3DX12_RESOURCE_DESC::Tex2D(SwapDepthStencilFormat, width, height, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
        D3D12_CLEAR_VALUE depthStencilClearValue = CD3DX12_CLEAR_VALUE(SwapDepthStencilFormat, 1.0F, 0x00);
        D3D12_HEAP_PROPERTIES depthStencilHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        if (FAILED(device->CreateCommittedResource(&depthStencilHeap, D3D12_HEAP_FLAG_NONE, &depthStencilDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthStencilClearValue, IID_PPV_ARGS(&depthStencilTarget))))
        {
            printf("D3D12 swap depth buffer resize failed\n");
            return false;
        }

        // Recreate swap resources
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvHeap->GetCPUDescriptorHandleForHeapStart());
        for (uint32_t frameIdx = 0; frameIdx < FrameCount; frameIdx++)
        {
            if (FAILED(swapchain->GetBuffer(frameIdx, IID_PPV_ARGS(&renderTargets[frameIdx]))))
            {
                printf("D3D12 get swap buffer failed\n");
                return false;
            }

            D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
            rtvDesc.Format = SwapColorSRGBFormat;
            rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            rtvDesc.Texture2D.MipSlice = 0;
            rtvDesc.Texture2D.PlaneSlice = 0;

            device->CreateRenderTargetView(renderTargets[frameIdx].Get(), &rtvDesc, rtvHandle);
            rtvHandle.Offset(1, rtvHeapIncrementSize);
        }

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
        dsvDesc.Format = SwapDepthStencilFormat;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Texture2D.MipSlice = 0;

        CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(dsvHeap->GetCPUDescriptorHandleForHeapStart());
        device->CreateDepthStencilView(depthStencilTarget.Get(), &dsvDesc, dsvHandle);

        return true;
    }

    void waitForGPU()
    {
        if (commandQueue == nullptr || fenceEvent == nullptr)
        {
            return;
        }

        uint64_t const currentValue = fenceValue;
        commandQueue->Signal(fence.Get(), currentValue);

        if (fence->GetCompletedValue() < currentValue)
        {
            fence->SetEventOnCompletion(currentValue, fenceEvent);
            WaitForSingleObjectEx(fenceEvent, INFINITE, FALSE);
        }

        fenceValue++;
    }
}

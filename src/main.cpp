#include <cstdint>
#include <cstdio>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_RADIANS
#define SDL_MAIN_HANDLED
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <SDL.h>
#include <SDL_syswm.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <wrl.h>
#include <dxgi1_6.h>
#include <directx/d3d12.h>
#include <directx/d3dx12.h>
#include <d3dcompiler.h>

#include "timer.hpp"

#define sizeof_array(val)   (sizeof((val)) / sizeof((val)[0]))

using Microsoft::WRL::ComPtr;

namespace Engine
{
    /// @brief Interleaved vertex data.
    struct Vertex
    {
        float position[3]; //< local position
        float color[3]; //< vertex color
        float normal[3]; //< local normal
        float tangent[3]; //< local tangent
        float texCoord[2]; //< texture coordinate
    };

    /// @brief Scene constant buffer data.
    struct alignas(256) SceneData
    {
        glm::mat4 viewproject; //< Camera view project matrix
        glm::mat4 model; //< Model transform
        glm::mat4 normal; //< normal / tangent transform
    };

    constexpr char const* WindowTitle = "Big Renderer";
    constexpr uint32_t DefaultWindowWidth = 1600;
    constexpr uint32_t DefaultWindowHeight = 900;

    constexpr D3D_FEATURE_LEVEL MinFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    constexpr DXGI_GPU_PREFERENCE GPUPreference = DXGI_GPU_PREFERENCE_UNSPECIFIED;
    constexpr DXGI_FORMAT SwapColorFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
    constexpr DXGI_FORMAT SwapColorSRGBFormat = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    constexpr DXGI_FORMAT SwapDepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    constexpr uint32_t FrameCount = 3;

    bool isRunning = true;
    SDL_Window* window = nullptr;
    Timer frameTimer;

    ComPtr<IDXGIFactory6> dxgiFactory;
    ComPtr<IDXGIAdapter1> dxgiAdapter;
    ComPtr<ID3D12Device> device;

    ComPtr<ID3D12CommandQueue> commandQueue;
    uint32_t rtvHeapIncrementSize = 0;
    uint32_t dsvHeapIncrementSize = 0;
    uint32_t cbvHeapIncrementSize = 0; //< also for SRVs & UAVs

    BOOL tearingSupport = FALSE;
    ComPtr<IDXGISwapChain4> swapchain;
    ComPtr<ID3D12Resource> renderTargets[FrameCount];
    ComPtr<ID3D12Resource> depthStencilTarget;
    ComPtr<ID3D12DescriptorHeap> rtvHeap; //< for swap rtvs
    ComPtr<ID3D12DescriptorHeap> dsvHeap; //< for swap dsv(s)

    ComPtr<ID3D12CommandAllocator> commandAllocator;
    ComPtr<ID3D12GraphicsCommandList> commandList;

    ComPtr<ID3D12Fence> fence;
    HANDLE fenceEvent = nullptr;
    uint64_t fenceValue = 0;

    // Per pass data
    ComPtr<ID3D12RootSignature> rootSignature; //< determines shader bind points
    ComPtr<ID3D12PipelineState> graphicsPipeline; //< determines pipeline stages & programming
    D3D12_VIEWPORT viewport;
    D3D12_RECT scissor;

    // Per scene data
    ComPtr<ID3D12DescriptorHeap> sceneDataCBVHeap;
    ComPtr<ID3D12Resource> sceneDataBuffer;
    D3D12_CONSTANT_BUFFER_VIEW_DESC sceneDataBufferView;

    // Per mesh data
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    ComPtr<ID3D12Resource> vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView;
    ComPtr<ID3D12Resource> indexBuffer;
    D3D12_INDEX_BUFFER_VIEW indexBufferView;

    // CPU side renderer data (temp for spinny thing)
    uint32_t screenWidth = DefaultWindowWidth;
    uint32_t screenHeight = DefaultWindowHeight;
    float rotationSpeed = 45.0F;
    float rotation = 0.0F;
    SceneData sceneData = SceneData{};

    namespace D3D12Helpers
    {
        void waitForGPU(ID3D12CommandQueue* queue, HANDLE event, uint64_t& value)
        {
            if (queue == nullptr || event == nullptr)
            {
                return;
            }

            uint64_t const currentValue = value;
            commandQueue->Signal(fence.Get(), currentValue);

            if (fence->GetCompletedValue() < currentValue)
            {
                fence->SetEventOnCompletion(currentValue, event);
                WaitForSingleObjectEx(event, INFINITE, FALSE);
            }

            value++;
        }
    } // namespace D3D12Helpers

    bool init()
    {
        if (SDL_Init(SDL_INIT_VIDEO) != 0)
        {
            printf("SDL init failed: %s\n", SDL_GetError());
            return false;
        }

        window = SDL_CreateWindow(
            WindowTitle,
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            DefaultWindowWidth, DefaultWindowHeight,
            SDL_WINDOW_RESIZABLE
        );
        if (window == nullptr)
        {
            printf("SDL window create failed: %s\n", SDL_GetError());
            return false;
        }

        // Create DXGI factory
        uint32_t factoryFlags = 0;
        ComPtr<ID3D12Debug1> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
        {
            factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
            debug->EnableDebugLayer();
            debug->SetEnableGPUBasedValidation(TRUE);
        }

        if (FAILED(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&dxgiFactory))))
        {
            printf("DXGI factory create failed\n");
            return false;
        }

        // auto select adapter
        {
            ComPtr<IDXGIAdapter1> adapter;
            for (UINT adapterIdx = 0; SUCCEEDED(dxgiFactory->EnumAdapterByGpuPreference(adapterIdx, GPUPreference, IID_PPV_ARGS(&adapter))); adapterIdx++)
            {
                DXGI_ADAPTER_DESC1 desc{};
                adapter->GetDesc1(&desc);

                if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
                    continue;
                }

                if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), MinFeatureLevel, __uuidof(ID3D12Device), nullptr)))
                {
                    dxgiAdapter = adapter;
                    printf("Automagically selected adapter: %ls\n", desc.Description);
                    break;
                }
            }
        }

        // Create device & command queue
        if (dxgiAdapter == nullptr)
        {
            printf("DXGI adapter select failed\n");
            return false;
        }

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
        cbvHeapIncrementSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        // Create swap chain
        SDL_SysWMinfo wmInfo{};
        SDL_VERSION(&wmInfo.version);
        if (!SDL_GetWindowWMInfo(window, &wmInfo))
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
        swapchainDesc.Width = DefaultWindowWidth;
        swapchainDesc.Height = DefaultWindowHeight;
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

        D3D12_HEAP_PROPERTIES defaultHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        D3D12_RESOURCE_DESC depthStencilDesc = CD3DX12_RESOURCE_DESC::Tex2D(SwapDepthStencilFormat, DefaultWindowWidth, DefaultWindowHeight, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
        D3D12_CLEAR_VALUE depthStencilClearValue = CD3DX12_CLEAR_VALUE(SwapDepthStencilFormat, 1.0F, 0x00);
        if (FAILED(device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &depthStencilDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthStencilClearValue, IID_PPV_ARGS(&depthStencilTarget))))
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

        // Create root signature
        CD3DX12_DESCRIPTOR_RANGE1 cbvRange; cbvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
        CD3DX12_ROOT_PARAMETER1 cbvScene; cbvScene.InitAsDescriptorTable(1, &cbvRange, D3D12_SHADER_VISIBILITY_VERTEX);

        D3D12_ROOT_PARAMETER1 rootParameters[] = { cbvScene };
        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc{};
        rootSignatureDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        rootSignatureDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
            | D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS
            | D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS
            | D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS
            | D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS
            | D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS;
        rootSignatureDesc.Desc_1_1.NumParameters = sizeof_array(rootParameters);
        rootSignatureDesc.Desc_1_1.pParameters = rootParameters;
        rootSignatureDesc.Desc_1_1.NumStaticSamplers = 0;
        rootSignatureDesc.Desc_1_1.pStaticSamplers = nullptr;

        ComPtr<ID3DBlob> rootSignatureBlob;
        ComPtr<ID3DBlob> rootSignatureError;
        if (FAILED(D3D12SerializeVersionedRootSignature(&rootSignatureDesc, &rootSignatureBlob, &rootSignatureError))
            || FAILED(device->CreateRootSignature(0x00, rootSignatureBlob->GetBufferPointer(), rootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature))))
        {
            printf("D3D12 root signature create failed\n");
            if (rootSignatureError != nullptr) {
                printf("Root signature error:\n%s\n", (char*)(rootSignatureError->GetBufferPointer()));
            }

            return false;
        }

        // Create graphics pipeline
        ComPtr<ID3DBlob> vertexShader;
        ComPtr<ID3DBlob> pixelShader;
        ComPtr<ID3DBlob> shaderError;

        uint32_t compileFlags = 0;
#ifndef NDEBUG
        compileFlags |= D3DCOMPILE_DEBUG
            | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
        if (FAILED(D3DCompileFromFile(L"../data/shader.hlsl", nullptr, nullptr, "VSForward", "vs_5_1", compileFlags, 0, &vertexShader, &shaderError))
            || FAILED(D3DCompileFromFile(L"../data/shader.hlsl", nullptr, nullptr, "PSForward", "ps_5_1", compileFlags, 0, &pixelShader, &shaderError)))
        {
            printf("D3D12 shader compilation failed\n");
            if (shaderError != nullptr) {
                printf("Shader error:\n%s\n", (char*)(shaderError->GetBufferPointer()));
            }

            return false;
        }

        D3D12_INPUT_ELEMENT_DESC inputElements[] = {
            D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, position), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            D3D12_INPUT_ELEMENT_DESC{ "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, color), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            D3D12_INPUT_ELEMENT_DESC{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, normal), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            D3D12_INPUT_ELEMENT_DESC{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, tangent), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Vertex, texCoord), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineDesc{};
        graphicsPipelineDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
        graphicsPipelineDesc.pRootSignature = rootSignature.Get();
        graphicsPipelineDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
        graphicsPipelineDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
        graphicsPipelineDesc.StreamOutput = D3D12_STREAM_OUTPUT_DESC{};
        graphicsPipelineDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        graphicsPipelineDesc.SampleMask = UINT32_MAX;
        graphicsPipelineDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        graphicsPipelineDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        graphicsPipelineDesc.RasterizerState.FrontCounterClockwise = FALSE;
        graphicsPipelineDesc.RasterizerState.DepthBias = 0;
        graphicsPipelineDesc.RasterizerState.DepthBiasClamp = 0.0F;
        graphicsPipelineDesc.RasterizerState.SlopeScaledDepthBias = 0.0F;
        graphicsPipelineDesc.RasterizerState.DepthClipEnable = TRUE;
        graphicsPipelineDesc.RasterizerState.MultisampleEnable = FALSE;
        graphicsPipelineDesc.RasterizerState.AntialiasedLineEnable = FALSE;
        graphicsPipelineDesc.RasterizerState.ForcedSampleCount = 0;
        graphicsPipelineDesc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
        graphicsPipelineDesc.DepthStencilState.DepthEnable = TRUE;
        graphicsPipelineDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        graphicsPipelineDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        graphicsPipelineDesc.DepthStencilState.StencilEnable = FALSE;
        graphicsPipelineDesc.DepthStencilState.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
        graphicsPipelineDesc.DepthStencilState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
        graphicsPipelineDesc.DepthStencilState.FrontFace = D3D12_DEPTH_STENCILOP_DESC{ D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS };
        graphicsPipelineDesc.DepthStencilState.BackFace = D3D12_DEPTH_STENCILOP_DESC{ D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS };
        graphicsPipelineDesc.InputLayout.NumElements = sizeof_array(inputElements);
        graphicsPipelineDesc.InputLayout.pInputElementDescs = inputElements;
        graphicsPipelineDesc.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
        graphicsPipelineDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        graphicsPipelineDesc.NumRenderTargets = 1;
        graphicsPipelineDesc.RTVFormats[0] = SwapColorSRGBFormat;
        graphicsPipelineDesc.DSVFormat = SwapDepthStencilFormat;
        graphicsPipelineDesc.SampleDesc.Count = 1;
        graphicsPipelineDesc.SampleDesc.Quality = 0;
        graphicsPipelineDesc.NodeMask = 0x00;

        if (FAILED(device->CreateGraphicsPipelineState(&graphicsPipelineDesc, IID_PPV_ARGS(&graphicsPipeline))))
        {
            printf("D3D12 graphics pipeline create failed\n");
            return false;
        }

        // Set up render viewport & scissor
        viewport = CD3DX12_VIEWPORT(0.0F, 0.0F, static_cast<float>(DefaultWindowWidth), static_cast<float>(DefaultWindowHeight), 0.0F, 1.0F);
        scissor = CD3DX12_RECT(0, 0, DefaultWindowWidth, DefaultWindowHeight);

        // Cache heap properties for upload heap
        D3D12_HEAP_PROPERTIES uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

        // Create scene data cbv & load scene data
        D3D12_DESCRIPTOR_HEAP_DESC sceneDataCBVHeapDesc{};
        sceneDataCBVHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        sceneDataCBVHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        sceneDataCBVHeapDesc.NumDescriptors = 1;
        sceneDataCBVHeapDesc.NodeMask = 0x00;

        if (FAILED(device->CreateDescriptorHeap(&sceneDataCBVHeapDesc, IID_PPV_ARGS(&sceneDataCBVHeap))))
        {
            printf("D3D12 scene data cbv heap create failed\n");
            return false;
        }

        uint32_t const sceneDataBufferSize = sizeof(SceneData);
        D3D12_RESOURCE_DESC sceneDataBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sceneDataBufferSize);
        if (FAILED(device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &sceneDataBufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&sceneDataBuffer))))
        {
            printf("D3D12 scene data buffer create failed\n");
            return false;
        }

        sceneDataBufferView = D3D12_CONSTANT_BUFFER_VIEW_DESC{ sceneDataBuffer->GetGPUVirtualAddress(), sceneDataBufferSize };
        device->CreateConstantBufferView(&sceneDataBufferView, CD3DX12_CPU_DESCRIPTOR_HANDLE(sceneDataCBVHeap->GetCPUDescriptorHandleForHeapStart(), 0, cbvHeapIncrementSize));

        // Load mesh data
        Vertex const vertices[] = {
            Vertex{ { -1.0F, -1.0F,  1.0F }, { 1.0F, 0.0F, 0.0F }, { 0.0F, 0.0F, 1.0F }, { 1.0F, 0.0F, 0.0F }, { 0.0F, 0.0F } },
            Vertex{ { -1.0F,  1.0F,  1.0F }, { 0.0F, 1.0F, 0.0F }, { 0.0F, 0.0F, 1.0F }, { 1.0F, 0.0F, 0.0F }, { 0.0F, 1.0F } },
            Vertex{ {  1.0F,  1.0F,  1.0F }, { 0.0F, 0.0F, 1.0F }, { 0.0F, 0.0F, 1.0F }, { 1.0F, 0.0F, 0.0F }, { 1.0F, 1.0F } },
            Vertex{ {  1.0F, -1.0F,  1.0F }, { 1.0F, 1.0F, 1.0F }, { 0.0F, 0.0F, 1.0F }, { 1.0F, 0.0F, 0.0F }, { 1.0F, 0.0F } },
        };

        uint32_t const indices[] = {
            0, 1, 2,
            2, 3, 0,
        };

        vertexCount = sizeof_array(vertices);
        indexCount = sizeof_array(indices);
        uint32_t const vertexBufferSize = sizeof_array(vertices) * sizeof(Vertex);
        uint32_t const indexBufferSize = sizeof_array(indices) * sizeof(uint32_t);

        D3D12_RESOURCE_DESC vertexBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);
        if (FAILED(device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &vertexBufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertexBuffer))))
        {
            printf("D3D12 vertex buffer create failed\n");
            return false;
        }

        D3D12_RESOURCE_DESC indexBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize);
        if (FAILED(device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &indexBufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&indexBuffer))))
        {
            printf("D3D12 index buffer create failed\n");
            return false;
        }

        vertexBufferView = D3D12_VERTEX_BUFFER_VIEW{ vertexBuffer->GetGPUVirtualAddress(), vertexBufferSize, sizeof(Vertex) };
        indexBufferView = D3D12_INDEX_BUFFER_VIEW{ indexBuffer->GetGPUVirtualAddress(), indexBufferSize, DXGI_FORMAT_R32_UINT };

        D3D12_RANGE vertexReadRange = CD3DX12_RANGE(0, 0);
        void* pVertexData = nullptr;
        if (FAILED(vertexBuffer->Map(0, &vertexReadRange, &pVertexData)))
        {
            printf("D3D12 vertex buffer map failed\n");
            return false;
        }

        memcpy(pVertexData, vertices, vertexBufferSize);
        vertexBuffer->Unmap(0, nullptr);

        D3D12_RANGE indexReadRange = CD3DX12_RANGE(0, 0);
        void* pIndexData = nullptr;
        if (FAILED(indexBuffer->Map(0, &indexReadRange, &pIndexData)))
        {
            printf("D3D12 index buffer map failed\n");
            return false;
        }

        memcpy(pIndexData, indices, indexBufferSize);
        indexBuffer->Unmap(0, nullptr);

        D3D12Helpers::waitForGPU(commandQueue.Get(), fenceEvent, fenceValue); //< wait for GPU queue just to be sure all uploads are finished
        printf("Initialized Big Renderer\n");
        return true;
    }

    void shutdown()
    {
        printf("Shutting down Big Renderer\n");

        D3D12Helpers::waitForGPU(commandQueue.Get(), fenceEvent, fenceValue);
        CloseHandle(fenceEvent);

        SDL_DestroyWindow(window);
        SDL_Quit();
    }

    void resize(uint32_t width, uint32_t height)
    {
        D3D12Helpers::waitForGPU(commandQueue.Get(), fenceEvent, fenceValue);

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
            isRunning = false;
            return;
        }

        // Recreate swap resources
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvHeap->GetCPUDescriptorHandleForHeapStart());
        for (uint32_t frameIdx = 0; frameIdx < FrameCount; frameIdx++)
        {
            if (FAILED(swapchain->GetBuffer(frameIdx, IID_PPV_ARGS(&renderTargets[frameIdx]))))
            {
                printf("D3D12 get swap buffer failed\n");
                isRunning = false;
                return;
            }

            D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
            rtvDesc.Format = SwapColorSRGBFormat;
            rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            rtvDesc.Texture2D.MipSlice = 0;
            rtvDesc.Texture2D.PlaneSlice = 0;

            device->CreateRenderTargetView(renderTargets[frameIdx].Get(), &rtvDesc, rtvHandle);
            rtvHandle.Offset(1, rtvHeapIncrementSize);
        }

        // Set viewport & scissor
        viewport = CD3DX12_VIEWPORT(0.0F, 0.0F, static_cast<float>(width), static_cast<float>(height), 0.0F, 1.0F);
        scissor = CD3DX12_RECT(0, 0, width, height);

        // Update render data
        screenWidth = width;
        screenHeight = height;

        printf("Window resized (%d x %d)\n", width, height);
    }

    void update()
    {
        // Tick frame timer
        frameTimer.tick();

        // Update window state
        SDL_Event event{};
        while (SDL_PollEvent(&event) != 0)
        {
            if (event.type == SDL_QUIT)
            {
                printf("Exit requested\n");
                isRunning = false;
            }
            if (event.type == SDL_WINDOWEVENT)
            {
                switch (event.window.event)
                {
                case SDL_WINDOWEVENT_RESIZED:
                    resize(event.window.data1, event.window.data2);
                    break;
                default:
                    break;
                }
            }
        }

        // Update render data
        rotation += rotationSpeed * static_cast<float>(frameTimer.deltaTimeMS() / 1'000.0);
        sceneData.viewproject = glm::perspective(glm::radians(60.0F), (float)(screenWidth) / (float)(screenHeight), 0.1F, 10.0F)
            * glm::lookAt(glm::vec3(0.0F, 0.0F, 5.0F), glm::vec3(0.0F, 0.0F, 0.0F), glm::vec3(0.0F, 1.0F, 0.0F));
        sceneData.model = glm::rotate(glm::identity<glm::mat4>(), glm::radians(rotation), glm::vec3(0.0F, 1.0F, 0.0F));
        sceneData.normal = glm::mat4(glm::transpose(glm::inverse(glm::mat3(sceneData.model))));

        // Upload render data to GPU buffers
        D3D12_RANGE readRange = CD3DX12_RANGE(0, 0);
        void* pSceneData = nullptr;
        if (FAILED(sceneDataBuffer->Map(0, &readRange, &pSceneData)))
        {
            printf("D3D12 scene data map failed\n");
            isRunning = false;
        }

        memcpy(pSceneData, &sceneData, sizeof(SceneData));
        sceneDataBuffer->Unmap(0, nullptr);
    }

    void render()
    {
        D3D12Helpers::waitForGPU(commandQueue.Get(), fenceEvent, fenceValue);
        uint32_t const backbufferIndex = swapchain->GetCurrentBackBufferIndex();

        // Reset command list
        if (FAILED(commandAllocator->Reset())
            || FAILED(commandList->Reset(commandAllocator.Get(), nullptr)))
        {
            printf("D3D12 command list reset failed\n");
            isRunning = false;
            return;
        }
        
        // Record render commands
        {
            // Transition to render target state
            CD3DX12_RESOURCE_BARRIER swapRenderTargetBarrier = CD3DX12_RESOURCE_BARRIER::Transition(renderTargets[backbufferIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
            commandList->ResourceBarrier(1, &swapRenderTargetBarrier);

            // Get current swap RTV
            CD3DX12_CPU_DESCRIPTOR_HANDLE currentSwapRTV(rtvHeap->GetCPUDescriptorHandleForHeapStart(), backbufferIndex, rtvHeapIncrementSize);
            CD3DX12_CPU_DESCRIPTOR_HANDLE currentSwapDSV(dsvHeap->GetCPUDescriptorHandleForHeapStart(), 0, dsvHeapIncrementSize);
            float const clearColor[] = { 0.1F, 0.1F, 0.1F, 0.1F };
            commandList->OMSetRenderTargets(1, &currentSwapRTV, FALSE, &currentSwapDSV);
            commandList->ClearRenderTargetView(currentSwapRTV, clearColor, 0, nullptr);
            commandList->ClearDepthStencilView(currentSwapDSV, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0F, 0x00, 0, nullptr);

            // Set used descriptor heaps for this frame
            ID3D12DescriptorHeap* ppDescriptorHeaps[] = { sceneDataCBVHeap.Get() };
            commandList->SetDescriptorHeaps(sizeof_array(ppDescriptorHeaps), ppDescriptorHeaps);

            // Set root signature
            commandList->SetGraphicsRootSignature(rootSignature.Get());
            commandList->SetGraphicsRootDescriptorTable(0, sceneDataCBVHeap->GetGPUDescriptorHandleForHeapStart());

            // Set pipeline state
            commandList->SetPipelineState(graphicsPipeline.Get());
            commandList->RSSetViewports(1, &viewport);
            commandList->RSSetScissorRects(1, &scissor);

            // Draw mesh
            D3D12_VERTEX_BUFFER_VIEW pVertexBuffers[] = { vertexBufferView };
            commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            commandList->IASetVertexBuffers(0, sizeof_array(pVertexBuffers), pVertexBuffers);
            commandList->IASetIndexBuffer(&indexBufferView);
            commandList->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);

            // Transition to present state
            CD3DX12_RESOURCE_BARRIER swapPresentBarrier = CD3DX12_RESOURCE_BARRIER::Transition(renderTargets[backbufferIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
            commandList->ResourceBarrier(1, &swapPresentBarrier);
        }

        // Close command list
        if (FAILED(commandList->Close()))
        {
            printf("D3D12 command list close failed\n");
            isRunning = false;
            return;
        }

        // Execute & present
        ID3D12CommandList* ppCommandLists[] = { commandList.Get() };
        commandQueue->ExecuteCommandLists(sizeof_array(ppCommandLists), ppCommandLists);
        swapchain->Present(1, 0);
    }
} // namespace Engine

int main()
{
    if (!Engine::init())
    {
        Engine::shutdown();
        return 1;
    }

    while (Engine::isRunning)
    {
        Engine::update();
        Engine::render();
    }

    Engine::shutdown();
    return 0;
}

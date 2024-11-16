#include <cstdint>
#include <cstdio>
#include <vector>

#define SDL_MAIN_HANDLED
#define STB_IMAGE_IMPLEMENTATION
#define TINYOBJLOADER_IMPLEMENTATION
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_dx12.h>
#include <SDL.h>
#include <SDL_syswm.h>
#include <stb_image.h>
#include <tiny_obj_loader.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <wrl.h>
#include <dxgi1_6.h>
#include <directx/d3d12.h>
#include <directx/d3dx12.h>
#include <d3dcompiler.h>

#include "math.hpp"
#include "renderer.hpp"
#include "timer.hpp"

#define sizeof_array(val)   (sizeof((val)) / sizeof((val)[0]))

using Microsoft::WRL::ComPtr;

namespace Engine
{
    /// @brief Interleaved vertex data.
    struct Vertex
    {
        glm::vec3 position;
        glm::vec3 color;
        glm::vec3 normal;
        glm::vec3 tangent;
        glm::vec2 texCoord;
    };

    /// @brief Simple TRS transform.
    struct Transform
    {
        glm::vec3 position = glm::vec3(0.0F);
        glm::quat rotation = glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
        glm::vec3 scale = glm::vec3(1.0F);

        glm::mat4 matrix()
        {
            return glm::translate(glm::identity<glm::mat4>(), position)
                * glm::mat4_cast(rotation)
                * glm::scale(glm::identity<glm::mat4>(), scale);
        }
    };

    /// @brief Virtual camera.
    struct Camera
    {
        // Camera transform
        glm::vec3 position = glm::vec3(0.0F);
        glm::vec3 forward = glm::vec3(0.0F, 0.0F, 1.0F);
        glm::vec3 up = glm::vec3(0.0F, 1.0F, 0.0F);

        // Perspective camera data
        float FOVy = 60.0F;
        float aspectRatio = 1.0F;
        float zNear = 0.1F;
        float zFar = 100.0F;

        glm::mat4 viewproject()
        {
            return glm::perspective(glm::radians(FOVy), aspectRatio, zNear, zFar) * glm::lookAt(position, position + forward, up);
        }
    };

    /// @brief Mesh data with indexed vertices.
    struct Mesh
    {
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
        ComPtr<ID3D12Resource> vertexBuffer = nullptr;
        ComPtr<ID3D12Resource> indexBuffer = nullptr;
        D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};
        D3D12_INDEX_BUFFER_VIEW indexBufferView = {};

        void Mesh::destroy()
        {
            indexBuffer.Reset();
            vertexBuffer.Reset();
        }
    };

    /// @brief Scene constant buffer data.
    struct alignas(256) SceneData
    {
        alignas(4) glm::vec3 sunDirection;
        alignas(4) glm::vec3 cameraPosition;
        alignas(16) glm::mat4 viewproject;
        alignas(16) glm::mat4 model;
        alignas(16) glm::mat4 normal;
    };

    constexpr char const* WindowTitle = "DX12 Renderer";
    constexpr uint32_t DefaultWindowWidth = 1600;
    constexpr uint32_t DefaultWindowHeight = 900;

    bool isRunning = true;
    SDL_Window* window = nullptr;
    Timer frameTimer;

    // ImGui Heap
    ComPtr<ID3D12DescriptorHeap> ImGuiSRVHeap; //< SRV heap specifically for ImGUI usage

    // Per pass data
    ComPtr<ID3D12RootSignature> rootSignature; //< determines shader bind points
    ComPtr<ID3D12PipelineState> graphicsPipeline; //< determines pipeline stages & programming
    D3D12_VIEWPORT viewport;
    D3D12_RECT scissor;

    // Per scene data
    ComPtr<ID3D12DescriptorHeap> descriptorResourceHeap;
    ComPtr<ID3D12Resource> sceneDataBuffer;

    // Scene objects
    Camera camera;
    Transform transform;
    Mesh mesh;

    // Material data
    ComPtr<ID3D12Resource> colorTexture;
    ComPtr<ID3D12Resource> normalTexture;

    // CPU side renderer data
    float sunAzimuth = 0.0F;
    float sunZenith = 0.0F;
    SceneData sceneData = SceneData{};

    namespace D3D12Helpers
    {
        bool createMesh(Mesh& mesh, Vertex* pVertices, uint32_t vertexCount, uint32_t* pIndices, uint32_t indexCount)
        {
            assert(pVertices != nullptr);
            assert(pIndices != nullptr);
            assert(vertexCount > 0);
            assert(indexCount > 0);

            mesh = Mesh{};
            mesh.vertexCount = vertexCount;
            mesh.indexCount = indexCount;
            uint32_t const vertexBufferSize = vertexCount * sizeof(Vertex);
            uint32_t const indexBufferSize = indexCount * sizeof(uint32_t);

            D3D12_RESOURCE_DESC vertexBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);
            D3D12_HEAP_PROPERTIES vertexBufferHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            if (FAILED(Renderer::device->CreateCommittedResource(&vertexBufferHeap, D3D12_HEAP_FLAG_NONE, &vertexBufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mesh.vertexBuffer))))
            {
                printf("D3D12 vertex buffer create failed\n");
                return false;
            }

            D3D12_RESOURCE_DESC indexBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize);
            D3D12_HEAP_PROPERTIES indexBufferHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            if (FAILED(Renderer::device->CreateCommittedResource(&indexBufferHeap, D3D12_HEAP_FLAG_NONE, &indexBufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mesh.indexBuffer))))
            {
                printf("D3D12 index buffer create failed\n");
                return false;
            }

            D3D12_RANGE vertexReadRange = CD3DX12_RANGE(0, 0);
            void* pVertexData = nullptr;
            if (FAILED(mesh.vertexBuffer->Map(0, &vertexReadRange, &pVertexData)))
            {
                printf("D3D12 vertex buffer map failed\n");
                return false;
            }

            memcpy(pVertexData, pVertices, vertexBufferSize);
            mesh.vertexBuffer->Unmap(0, nullptr);

            D3D12_RANGE indexReadRange = CD3DX12_RANGE(0, 0);
            void* pIndexData = nullptr;
            if (FAILED(mesh.indexBuffer->Map(0, &indexReadRange, &pIndexData)))
            {
                printf("D3D12 index buffer map failed\n");
                return false;
            }

            memcpy(pIndexData, pIndices, indexBufferSize);
            mesh.indexBuffer->Unmap(0, nullptr);

            mesh.vertexBufferView = D3D12_VERTEX_BUFFER_VIEW{ mesh.vertexBuffer->GetGPUVirtualAddress(), vertexBufferSize, sizeof(Vertex) };
            mesh.indexBufferView = D3D12_INDEX_BUFFER_VIEW{ mesh.indexBuffer->GetGPUVirtualAddress(), indexBufferSize, DXGI_FORMAT_R32_UINT };

            return true;
        }

        bool loadOBJ(char const* path, Mesh& mesh)
        {
            tinyobj::ObjReader reader;
            tinyobj::ObjReaderConfig config;

            config.triangulate = true;
            config.triangulation_method = "earcut";
            config.vertex_color = true;

            if (!reader.ParseFromFile(path))
            {
                printf("TinyOBJ OBJ load failed [%s]\n", path);
                return false;
            }
            printf("Loaded OBJ mesh [%s]\n", path);

            auto const& attrib = reader.GetAttrib();
            auto const& shapes = reader.GetShapes();

            std::vector<Vertex> vertices;
            std::vector<uint32_t> indices;
            for (auto const& shape : shapes)
            {
                vertices.reserve(vertices.size() + shape.mesh.indices.size());
                indices.reserve(indices.size() + shape.mesh.indices.size());

                for (auto const& index : shape.mesh.indices)
                {
                    size_t vertexIdx = index.vertex_index * 3;
                    size_t normalIdx = index.normal_index * 3;
                    size_t texIdx = index.texcoord_index * 2;

                    vertices.push_back(Vertex{
                        { attrib.vertices[vertexIdx + 0], attrib.vertices[vertexIdx + 1], attrib.vertices[vertexIdx + 2] },
                        { attrib.colors[vertexIdx + 0], attrib.colors[vertexIdx + 1], attrib.colors[vertexIdx + 2] },
                        { attrib.normals[normalIdx + 0], attrib.normals[normalIdx + 1], attrib.normals[normalIdx + 2] },
                        { 0.0F, 0.0F, 0.0F }, //< tangents are calculated after loading
                        { attrib.texcoords[texIdx + 0], attrib.texcoords[texIdx + 1] },
                    });
                    indices.push_back(static_cast<uint32_t>(indices.size())); //< works because mesh is triangulated
                }
            }

            // calculate tangents based on position & texture coords
            assert(indices.size() % 3 == 0); //< Need multiple of 3 for triangle indices
            for (size_t i = 0; i < indices.size(); i += 3)
            {
                Vertex& v0 = vertices[indices[i + 0]];
                Vertex& v1 = vertices[indices[i + 1]];
                Vertex& v2 = vertices[indices[i + 2]];

                glm::vec3 const e1 = v1.position - v0.position;
                glm::vec3 const e2 = v2.position - v0.position;
                glm::vec2 const dUV1 = v1.texCoord - v0.texCoord;
                glm::vec2 const dUV2 = v2.texCoord - v0.texCoord;

                float const f = 1.0F / (dUV1.x * dUV2.y - dUV1.y * dUV2.x);
                glm::vec3 const tangent = f * (dUV2.y * e1 - dUV1.y * e2);

                v0.tangent = tangent;
                v1.tangent = tangent;
                v2.tangent = tangent;
            }

            return createMesh(mesh, vertices.data(), static_cast<uint32_t>(vertices.size()), indices.data(), static_cast<uint32_t>(indices.size()));
        }

        bool loadTexture(char const* path, ID3D12Resource** ppResource)
        {
            assert(path != nullptr);
            assert(ppResource != nullptr);

            int texWidth = 0;
            int texHeight = 0;
            int texChannels = 0;
            stbi_uc* pTextureData = stbi_load(path, &texWidth, &texHeight, &texChannels, 4); // XXX: assumes 4 channels always, is this OK?
            if (pTextureData == nullptr)
            {
                printf("STB Image texture load failed [%s]\n", path);
                return false;
            }
            printf("Loaded texture [%s] (%d x %d x %d)\n", path, texWidth, texHeight, texChannels);

            D3D12_RESOURCE_DESC textureDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, texWidth, texHeight, 1, 1);
            D3D12_HEAP_PROPERTIES textureHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
            if (FAILED(Renderer::device->CreateCommittedResource(&textureHeap, D3D12_HEAP_FLAG_NONE, &textureDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(ppResource))))
            {
                printf("D3D12 texture create failed\n");
                stbi_image_free(pTextureData);
                return false;
            }

            uint64_t uploadBufferSize = GetRequiredIntermediateSize(*ppResource, 0, 1);
            ComPtr<ID3D12Resource> uploadBuffer;
            D3D12_RESOURCE_DESC uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
            D3D12_HEAP_PROPERTIES uploadBufferHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            if (FAILED(Renderer::device->CreateCommittedResource(&uploadBufferHeap, D3D12_HEAP_FLAG_NONE, &uploadBufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer))))
            {
                printf("D3D12 texture upload buffer create failed\n");
                stbi_image_free(pTextureData);
                return false;
            }

            // Perform upload using transient commandlist
            {
                ComPtr<ID3D12GraphicsCommandList> uploadCommandList;
                if (FAILED(Renderer::device->CreateCommandList(0x00, D3D12_COMMAND_LIST_TYPE_DIRECT, Renderer::commandAllocator.Get(), nullptr, IID_PPV_ARGS(&uploadCommandList))))
                {
                    printf("D3D12 upload command list create failed\n");
                    stbi_image_free(pTextureData);
                    return false;
                }

                D3D12_SUBRESOURCE_DATA textureResourceData{};
                textureResourceData.pData = pTextureData;
                textureResourceData.RowPitch = texWidth * 4;
                textureResourceData.SlicePitch = texWidth * texHeight * 4;

                UpdateSubresources(uploadCommandList.Get(), *ppResource, uploadBuffer.Get(), 0, 0, 1, &textureResourceData);

                D3D12_RESOURCE_BARRIER textureUploadBarrier = CD3DX12_RESOURCE_BARRIER::Transition(*ppResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                uploadCommandList->ResourceBarrier(1, &textureUploadBarrier);
                
                if (FAILED(uploadCommandList->Close()))
                {
                    printf("D3D12 upload command list close failed\n");
                    stbi_image_free(pTextureData);
                    return false;
                }

                ID3D12CommandList* ppUploadCommandLists[] = { uploadCommandList.Get() };
                Renderer::commandQueue->ExecuteCommandLists(1, ppUploadCommandLists);
                Renderer::waitForGPU();
            }

            stbi_image_free(pTextureData);
            return true;
        }
    } // namespace D3D12Helpers

    bool init()
    {
        IMGUI_CHECKVERSION();

        ImGui::CreateContext();
        ImGui::StyleColorsDark();
        ImGuiIO& io = ImGui::GetIO(); (void)(io);
        io.IniFilename = nullptr;

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

        if (!ImGui_ImplSDL2_InitForD3D(window))
        {
            printf("ImGui init for SDL2 failed\n");
            return false;
        }

        if (!Renderer::init(window))
        {
            printf("Renderer init failed\n");
            return false;
        }

        // Init ImGui backend
        D3D12_DESCRIPTOR_HEAP_DESC ImGuiSRVHeapDesc{};
        ImGuiSRVHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ImGuiSRVHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        ImGuiSRVHeapDesc.NumDescriptors = 1;
        ImGuiSRVHeapDesc.NodeMask = 0x00;
        if (FAILED(Renderer::device->CreateDescriptorHeap(&ImGuiSRVHeapDesc, IID_PPV_ARGS(&ImGuiSRVHeap))))
        {
            printf("D3D12 ImGui SRV heap create failed\n");
            return false;
        }

        if (!ImGui_ImplDX12_Init(Renderer::device.Get(), 1, Renderer::SwapColorSRGBFormat, ImGuiSRVHeap.Get(), ImGuiSRVHeap->GetCPUDescriptorHandleForHeapStart(), ImGuiSRVHeap->GetGPUDescriptorHandleForHeapStart()))
        {
            printf("ImGui init for D3D12 failed\n");
            return false;
        }

        // Create root signature for graphics pipeline
        CD3DX12_DESCRIPTOR_RANGE1 sceneDataDescriptorRange;
        sceneDataDescriptorRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

        CD3DX12_DESCRIPTOR_RANGE1 textureDataDescriptorRange;
        textureDataDescriptorRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

        CD3DX12_ROOT_PARAMETER1 vsRootParameter;
        D3D12_DESCRIPTOR_RANGE1 vsRanges[] = { sceneDataDescriptorRange };
        vsRootParameter.InitAsDescriptorTable(sizeof_array(vsRanges), vsRanges, D3D12_SHADER_VISIBILITY_VERTEX);

        CD3DX12_ROOT_PARAMETER1 psRootParameter;
        D3D12_DESCRIPTOR_RANGE1 psRanges[] = { sceneDataDescriptorRange, textureDataDescriptorRange };
        psRootParameter.InitAsDescriptorTable(sizeof_array(psRanges), psRanges, D3D12_SHADER_VISIBILITY_PIXEL);

        D3D12_STATIC_SAMPLER_DESC textureSamplerDesc{};
        textureSamplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        textureSamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        textureSamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        textureSamplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        textureSamplerDesc.MipLODBias = 0.0F;
        textureSamplerDesc.MaxAnisotropy = 0;
        textureSamplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        textureSamplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
        textureSamplerDesc.MinLOD = 0.0F;
        textureSamplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
        textureSamplerDesc.ShaderRegister = 0;
        textureSamplerDesc.RegisterSpace = 0;
        textureSamplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_PARAMETER1 rootParameters[] = { vsRootParameter, psRootParameter, };
        D3D12_STATIC_SAMPLER_DESC staticSamplers[] = { textureSamplerDesc };
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
        rootSignatureDesc.Desc_1_1.NumStaticSamplers = sizeof_array(staticSamplers);
        rootSignatureDesc.Desc_1_1.pStaticSamplers = staticSamplers;

        ComPtr<ID3DBlob> rootSignatureBlob;
        ComPtr<ID3DBlob> rootSignatureError;
        if (FAILED(D3D12SerializeVersionedRootSignature(&rootSignatureDesc, &rootSignatureBlob, &rootSignatureError))
            || FAILED(Renderer::device->CreateRootSignature(0x00, rootSignatureBlob->GetBufferPointer(), rootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature))))
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
        if (FAILED(D3DCompileFromFile(L"data/shaders/shader.hlsl", nullptr, nullptr, "VSForward", "vs_5_0", compileFlags, 0, &vertexShader, &shaderError))
            || FAILED(D3DCompileFromFile(L"data/shaders/shader.hlsl", nullptr, nullptr, "PSForward", "ps_5_0", compileFlags, 0, &pixelShader, &shaderError)))
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
        graphicsPipelineDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
        graphicsPipelineDesc.RasterizerState.FrontCounterClockwise = TRUE;
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
        graphicsPipelineDesc.RTVFormats[0] = Renderer::SwapColorSRGBFormat;
        graphicsPipelineDesc.DSVFormat = Renderer::SwapDepthStencilFormat;
        graphicsPipelineDesc.SampleDesc.Count = 1;
        graphicsPipelineDesc.SampleDesc.Quality = 0;
        graphicsPipelineDesc.NodeMask = 0x00;

        if (FAILED(Renderer::device->CreateGraphicsPipelineState(&graphicsPipelineDesc, IID_PPV_ARGS(&graphicsPipeline))))
        {
            printf("D3D12 graphics pipeline create failed\n");
            return false;
        }

        // Set up render viewport & scissor
        viewport = CD3DX12_VIEWPORT(0.0F, 0.0F, static_cast<float>(DefaultWindowWidth), static_cast<float>(DefaultWindowHeight), 0.0F, 1.0F);
        scissor = CD3DX12_RECT(0, 0, DefaultWindowWidth, DefaultWindowHeight);

        // Create descriptor resource heap
        D3D12_DESCRIPTOR_HEAP_DESC descriptorResourceHeapDesc{};
        descriptorResourceHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        descriptorResourceHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        descriptorResourceHeapDesc.NumDescriptors = 3; // cbv + 2 textures
        descriptorResourceHeapDesc.NodeMask = 0x00;

        if (FAILED(Renderer::device->CreateDescriptorHeap(&descriptorResourceHeapDesc, IID_PPV_ARGS(&descriptorResourceHeap))))
        {
            printf("D3D12 scene data cbv heap create failed\n");
            return false;
        }

        // Create scene data buffer
        uint32_t const sceneDataBufferSize = sizeof(SceneData);
        D3D12_RESOURCE_DESC sceneDataBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sceneDataBufferSize);
        D3D12_HEAP_PROPERTIES sceneDataHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        if (FAILED(Renderer::device->CreateCommittedResource(&sceneDataHeap, D3D12_HEAP_FLAG_NONE, &sceneDataBufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&sceneDataBuffer))))
        {
            printf("D3D12 scene data buffer create failed\n");
            return false;
        }

        D3D12_CONSTANT_BUFFER_VIEW_DESC sceneDataBufferView = D3D12_CONSTANT_BUFFER_VIEW_DESC{ sceneDataBuffer->GetGPUVirtualAddress(), sceneDataBufferSize };
        Renderer::device->CreateConstantBufferView(&sceneDataBufferView, CD3DX12_CPU_DESCRIPTOR_HANDLE(descriptorResourceHeap->GetCPUDescriptorHandleForHeapStart(), 0, Renderer::cbvsrvHeapIncrementSize));

        // Set camera params
        camera.position = glm::vec3(0.0F, 0.0F, -5.0F);
        camera.forward = glm::normalize(glm::vec3(0.0F) - camera.position);
        camera.aspectRatio = static_cast<float>(DefaultWindowWidth) / static_cast<float>(DefaultWindowHeight);

        // Set transform state
        transform = Transform{};

        // Load mesh data
        if (!D3D12Helpers::loadOBJ("data/assets/suzanne.obj", mesh))
        {
            printf("Mesh load failed\n");
            return false;
        }

        // Load material data
        if (!D3D12Helpers::loadTexture("data/assets/brickwall.jpg", &colorTexture)) {
            printf("Color map load failed\n");
            return false;
        }

        if (!D3D12Helpers::loadTexture("data/assets/brickwall_normal.jpg", &normalTexture)) {
            printf("Normal map load failed\n");
            return false;
        }

        // Create material SRVs
        D3D12_SHADER_RESOURCE_VIEW_DESC colorTextureViewDesc{};
        colorTextureViewDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        colorTextureViewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        colorTextureViewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        colorTextureViewDesc.Texture2D.MostDetailedMip = 0;
        colorTextureViewDesc.Texture2D.MipLevels = 1;
        colorTextureViewDesc.Texture2D.PlaneSlice = 0;
        colorTextureViewDesc.Texture2D.ResourceMinLODClamp = 0.0F;
        Renderer::device->CreateShaderResourceView(colorTexture.Get(), &colorTextureViewDesc, CD3DX12_CPU_DESCRIPTOR_HANDLE(descriptorResourceHeap->GetCPUDescriptorHandleForHeapStart(), 1, Renderer::cbvsrvHeapIncrementSize));

        D3D12_SHADER_RESOURCE_VIEW_DESC normalTextureViewDesc{};
        normalTextureViewDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        normalTextureViewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        normalTextureViewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        normalTextureViewDesc.Texture2D.MostDetailedMip = 0;
        normalTextureViewDesc.Texture2D.MipLevels = 1;
        normalTextureViewDesc.Texture2D.PlaneSlice = 0;
        normalTextureViewDesc.Texture2D.ResourceMinLODClamp = 0.0F;
        Renderer::device->CreateShaderResourceView(normalTexture.Get(), &normalTextureViewDesc, CD3DX12_CPU_DESCRIPTOR_HANDLE(descriptorResourceHeap->GetCPUDescriptorHandleForHeapStart(), 2, Renderer::cbvsrvHeapIncrementSize));

        Renderer::waitForGPU(); // Wait until GPU uploads are finished
        printf("Initialized DX12 Renderer\n");
        return true;
    }

    void shutdown()
    {
        printf("Shutting down DX12 Renderer\n");

        Renderer::waitForGPU();

        normalTexture.Reset();
        colorTexture.Reset();
        mesh.destroy();
        ImGui_ImplDX12_Shutdown();

        Renderer::shutdown();

        ImGui_ImplSDL2_Shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();

        ImGui::DestroyContext();
    }

    void resize()
    {
        int width = 0;
        int height = 0;
        SDL_GetWindowSize(window, &width, &height);
        if (width == 0 || height == 0) {
            return;
        }

        printf("Window resized (%d x %d)\n", width, height);

        Renderer::waitForGPU();
        if (!Renderer::resizeSwapResources(static_cast<uint32_t>(width), static_cast<uint32_t>(height)))
        {
            printf("Swap resize failed\n");
            isRunning = false;
        }

        // Set viewport & scissor
        viewport = CD3DX12_VIEWPORT(0.0F, 0.0F, static_cast<float>(width), static_cast<float>(height), 0.0F, 1.0F);
        scissor = CD3DX12_RECT(0, 0, width, height);

        // Update camera aspect ratio
        camera.aspectRatio = static_cast<float>(width) / static_cast<float>(height);
    }

    void update()
    {
        // Tick frame timer
        frameTimer.tick();

        // Update window state
        SDL_Event event{};
        while (SDL_PollEvent(&event) != 0)
        {
            ImGui_ImplSDL2_ProcessEvent(&event);

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
                    resize();
                    break;
                default:
                    break;
                }
            }
        }

        // Record GUI state
        ImGui_ImplSDL2_NewFrame();
        ImGui_ImplDX12_NewFrame();
        ImGui::NewFrame();

        if (ImGui::Begin("DX12 Renderer Config"))
        {
            ImGui::SeparatorText("Statistics");
            ImGui::Text("Frame time: %10.2f ms", frameTimer.deltaTimeMS());
            ImGui::Text("FPS:        %10.2f fps", 1'000.0 / frameTimer.deltaTimeMS());

            ImGui::SeparatorText("Scene");
            ImGui::DragFloat("Sun Azimuth", &sunAzimuth, 1.0F, 0.0F, 360.0F);
            ImGui::DragFloat("Sun Zenith", &sunZenith, 1.0F, -90.0F, 90.0F);
        }
        ImGui::End();

        ImGui::Render();

        // Update camera data
        camera.position = glm::vec3(2.0F, 2.0F, -5.0F);
        camera.forward = glm::normalize(glm::vec3(0.0F) - camera.position);

        // Update transform data
        transform.rotation = glm::rotate(transform.rotation, (float)frameTimer.deltaTimeMS() / 1000.0F, glm::vec3(0.0F, 1.0F, 0.0F));

        // Update scene data
        sceneData.sunDirection = glm::normalize(glm::vec3{
            glm::cos(glm::radians(sunAzimuth)) * glm::sin(glm::radians(90.0F - sunZenith)),
            glm::cos(glm::radians(90.0F - sunZenith)),
            glm::sin(glm::radians(sunAzimuth))* glm::sin(glm::radians(90.0F - sunZenith)),
        });
        sceneData.cameraPosition = camera.position;
        sceneData.viewproject = camera.viewproject();
        sceneData.model = transform.matrix();
        sceneData.normal = glm::mat4(glm::inverse(glm::transpose(glm::mat3(sceneData.model))));

        // Upload render data to GPU visible buffers
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
        Renderer::waitForGPU();
        uint32_t const backbufferIndex = Renderer::swapchain->GetCurrentBackBufferIndex();

        // Reset command list
        if (FAILED(Renderer::commandAllocator->Reset())
            || FAILED(Renderer::commandList->Reset(Renderer::commandAllocator.Get(), nullptr)))
        {
            printf("D3D12 command list reset failed\n");
            isRunning = false;
            return;
        }
        
        // Record render commands
        {
            // Transition to render target state
            CD3DX12_RESOURCE_BARRIER swapRenderTargetBarrier = CD3DX12_RESOURCE_BARRIER::Transition(Renderer::renderTargets[backbufferIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
            Renderer::commandList->ResourceBarrier(1, &swapRenderTargetBarrier);

            // Get current swap RTV
            CD3DX12_CPU_DESCRIPTOR_HANDLE currentSwapRTV(Renderer::rtvHeap->GetCPUDescriptorHandleForHeapStart(), backbufferIndex, Renderer::rtvHeapIncrementSize);
            CD3DX12_CPU_DESCRIPTOR_HANDLE currentSwapDSV(Renderer::dsvHeap->GetCPUDescriptorHandleForHeapStart(), 0, Renderer::dsvHeapIncrementSize);
            float const clearColor[] = { 0.1F, 0.1F, 0.1F, 0.1F };
            Renderer::commandList->OMSetRenderTargets(1, &currentSwapRTV, FALSE, &currentSwapDSV);
            Renderer::commandList->ClearRenderTargetView(currentSwapRTV, clearColor, 0, nullptr);
            Renderer::commandList->ClearDepthStencilView(currentSwapDSV, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0F, 0x00, 0, nullptr);

            // Set used descriptor heaps for this frame
            ID3D12DescriptorHeap* ppDescriptorHeaps[] = { descriptorResourceHeap.Get() };
            Renderer::commandList->SetDescriptorHeaps(sizeof_array(ppDescriptorHeaps), ppDescriptorHeaps);

            // Set root signature
            Renderer::commandList->SetGraphicsRootSignature(rootSignature.Get());
            Renderer::commandList->SetGraphicsRootDescriptorTable(0, descriptorResourceHeap->GetGPUDescriptorHandleForHeapStart());
            Renderer::commandList->SetGraphicsRootDescriptorTable(1, descriptorResourceHeap->GetGPUDescriptorHandleForHeapStart());

            // Set pipeline state
            Renderer::commandList->SetPipelineState(graphicsPipeline.Get());
            Renderer::commandList->RSSetViewports(1, &viewport);
            Renderer::commandList->RSSetScissorRects(1, &scissor);

            // Draw mesh
            D3D12_VERTEX_BUFFER_VIEW pVertexBuffers[] = { mesh.vertexBufferView };
            Renderer::commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            Renderer::commandList->IASetVertexBuffers(0, sizeof_array(pVertexBuffers), pVertexBuffers);
            Renderer::commandList->IASetIndexBuffer(&mesh.indexBufferView);
            Renderer::commandList->DrawIndexedInstanced(mesh.indexCount, 1, 0, 0, 0);

            // Draw GUI
            ID3D12DescriptorHeap* ppImGuiHeaps[] = { ImGuiSRVHeap.Get() };
            Renderer::commandList->SetDescriptorHeaps(sizeof_array(ppImGuiHeaps), ppImGuiHeaps);
            ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), Renderer::commandList.Get());

            // Transition to present state
            CD3DX12_RESOURCE_BARRIER swapPresentBarrier = CD3DX12_RESOURCE_BARRIER::Transition(Renderer::renderTargets[backbufferIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
            Renderer::commandList->ResourceBarrier(1, &swapPresentBarrier);
        }

        // Close command list
        if (FAILED(Renderer::commandList->Close()))
        {
            printf("D3D12 command list close failed\n");
            isRunning = false;
            return;
        }

        // Execute & present
        ID3D12CommandList* ppCommandLists[] = { Renderer::commandList.Get() };
        Renderer::commandQueue->ExecuteCommandLists(sizeof_array(ppCommandLists), ppCommandLists);
        Renderer::swapchain->Present(1, 0);
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

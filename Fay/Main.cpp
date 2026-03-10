#include <SDL3/SDL.h>
#include <memory>
#include <nvrhi/utils.h>
#include "Platform/FileReader.h"
#include "Platform/Window.h"
#include "Common/Profiling.h"
#include "Common/Log.h"
#include "Graphics/RendererBase.h"
#include "SimpleMath.h"
#include "App.h"
#include "Scene/GLTFImporter.h"
#include "Scene/SceneGraph.h"

namespace SM = DirectX::SimpleMath;

static constexpr inline nvrhi::GraphicsAPI GetPlatformAPI()
{
#if FAY_HAS_D3D
	return nvrhi::GraphicsAPI::D3D12;
#else
	return nvrhi::GraphicsAPI::VULKAN;
#endif
}


class ClearPass : public fay::IRenderPass
{
public:
    ClearPass(fay::Renderer* renderer, nvrhi::Color clearColor)
        : IRenderPass(renderer), m_clearColor(clearColor)
    {
        auto device = GetRenderer()->GetDevice();
        m_cmdList = device->createCommandList();
    }

    void OnRender(nvrhi::IFramebuffer* framebuffer) override
    {
        m_cmdList->open();

        nvrhi::utils::ClearColorAttachment(m_cmdList, framebuffer, 0, m_clearColor);
        m_cmdList->close();

        GetRenderer()->GetDevice()->executeCommandList(m_cmdList);
    }

    std::string_view GetName() const override { return "ClearPass"; }
    bool SupportsDepthBuffer() override { return true; }
    void OnBackBufferResizeBegin() override {}
    void OnBackBufferResizeEnd(
        [[maybe_unused]] const fay::u32 width, 
        [[maybe_unused]] const fay::u32 height, 
        [[maybe_unused]] const fay::u32 sampleCount) override {}
    
private:
    nvrhi::Color m_clearColor;
    nvrhi::CommandListHandle m_cmdList;
};


class DrawPass : public fay::IRenderPass
{
public:
    DrawPass(fay::Renderer* renderer) : IRenderPass(renderer)
    {
        using namespace fay;

        auto device = GetRenderer()->GetDevice();
        
        // Load shaders
        {
            FileReader::ReadResult result = FileReader::Read("Shaders/TriangleVS.cso");
            if (!result)
            {
                Log::Error("Failed to read shader file: {}", result.error());
            }

            auto& data = result.value();

            nvrhi::ShaderDesc desc;
            desc.setDebugName("TriangleVS").setEntryName("VSMain").setShaderType(nvrhi::ShaderType::Vertex);
            m_vs = device->createShader(desc, data.data(), data.size());

            result = FileReader::Read("Shaders/TrianglePS.cso");
            if (!result)
            {
                Log::Error("Failed to read shader file: {}", result.error());
            }

            data = result.value();

            desc.setDebugName("TrianglePS").setEntryName("PSMain").setShaderType(nvrhi::ShaderType::Pixel);
            m_ps = device->createShader(desc, data.data(), data.size());
        }

        // Create buffers
        {
            // Constant buffer
            m_cb = device->createBuffer(
                nvrhi::utils::CreateStaticConstantBufferDesc(sizeof(ConstantBufferEntry), "ConstantBuffer")
                    .setInitialState(nvrhi::ResourceStates::ConstantBuffer)
                    .setKeepInitialState(true));

            // Input layout
            std::array<nvrhi::VertexAttributeDesc, 2> attributes =
            {
                nvrhi::VertexAttributeDesc()
                    .setName("POSITION")
                    .setFormat(nvrhi::Format::RGB32_FLOAT)
                    .setOffset(0)
                    .setBufferIndex(0)
                    .setElementStride(sizeof(Vertex)),

                nvrhi::VertexAttributeDesc()
                    .setName("UV")
                    .setFormat(nvrhi::Format::RG32_FLOAT)
                    .setOffset(0)
                    .setBufferIndex(1)
                    .setElementStride(sizeof(Vertex))
            };
            m_inputLayout = device->createInputLayout(attributes.data(), (u32)attributes.size(), m_vs);

            // Vertex buffer
            nvrhi::BufferDesc vbDesc;
            vbDesc.setByteSize(sizeof(g_Vertices))
                .setIsVertexBuffer(true)
                .setDebugName("VertexBuffer")
                .setInitialState(nvrhi::ResourceStates::CopyDest);
            m_vb = device->createBuffer(vbDesc);

			// Index buffer
			nvrhi::BufferDesc ibDesc;
			ibDesc.setByteSize(sizeof(g_Indices))
				.setIsIndexBuffer(true)
				.setDebugName("IndexBuffer")
				.setInitialState(nvrhi::ResourceStates::CopyDest);
			m_ib = device->createBuffer(ibDesc);
        }

        m_cmdList = device->createCommandList();
        m_cmdList->open();
        
        m_cmdList->beginTrackingBufferState(m_vb, nvrhi::ResourceStates::CopyDest);
        m_cmdList->writeBuffer(m_vb , g_Vertices, sizeof(g_Vertices));
        m_cmdList->setPermanentBufferState(m_vb, nvrhi::ResourceStates::VertexBuffer);

		m_cmdList->beginTrackingBufferState(m_ib, nvrhi::ResourceStates::CopyDest);
		m_cmdList->writeBuffer(m_ib, g_Indices, sizeof(g_Indices));
		m_cmdList->setPermanentBufferState(m_ib, nvrhi::ResourceStates::IndexBuffer);

        m_cmdList->close();
        device->executeCommandList(m_cmdList);

        nvrhi::BindingSetDesc bsDesc;
        bsDesc.addItem(
            nvrhi::BindingSetItem::ConstantBuffer(
                0, m_cb,
                nvrhi::BufferRange(0, sizeof(ConstantBufferEntry))));

        if (!nvrhi::utils::CreateBindingSetAndLayout(device, nvrhi::ShaderType::All, 0, bsDesc, m_bindingLayout, m_bindingSet))
        {
            Log::Error("Failed to create binding set or layout!");
        }
    }

    std::string_view GetName() const override { return "DrawPass"; }
    
    void OnRender(nvrhi::IFramebuffer* framebuffer) override
    {
        auto device = GetRenderer()->GetDevice();

        if (!m_pipeline)
        {
            auto psoDesc = nvrhi::GraphicsPipelineDesc()
                .setVertexShader(m_vs)
                .setPixelShader(m_ps)
                .setInputLayout(m_inputLayout)
                .addBindingLayout(m_bindingLayout)
                .setPrimType(nvrhi::PrimitiveType::TriangleList)
                .setRenderState(nvrhi::RenderState()
                    .setDepthStencilState(nvrhi::DepthStencilState()
                        .setDepthTestEnable(false)));

            m_pipeline = device->createGraphicsPipeline(psoDesc, framebuffer->getFramebufferInfo());
        }

        m_cmdList->open();
        
        // Create matrices
        const SM::Matrix world = SM::Matrix::CreateScale(5.0f)
            * SM::Matrix::CreateRotationY(DirectX::XMConvertToRadians(t += 0.1f))
            * SM::Matrix::CreateTranslation(SM::Vector3::Zero);

        const SM::Matrix view = SM::Matrix::CreateLookAt(
            SM::Vector3(15.0f, 0.0f, 0.0f),
            SM::Vector3::Zero,
            SM::Vector3::Up);

        const SM::Matrix proj = SM::Matrix::CreatePerspectiveFieldOfView(
			DirectX::XMConvertToRadians(45.0f),
			1280.0f / 720.0f,
			0.1f, 1000.0f);

        // Update CB
        ConstantBufferEntry cbEntry{ .ViewProjMatrix = (world * view * proj).Transpose() };
		m_cmdList->writeBuffer(m_cb, &cbEntry, sizeof(cbEntry));

        // Build graphics state
        nvrhi::GraphicsState state;
        state.addBindingSet(m_bindingSet)
            .setIndexBuffer(nvrhi::IndexBufferBinding()
                .setBuffer(m_ib)
                .setFormat(nvrhi::Format::R32_UINT)
                .setOffset(0))
            .addVertexBuffer(nvrhi::VertexBufferBinding()
                .setBuffer(m_vb)
                .setSlot(1)
                .setOffset(offsetof(Vertex, UV)))
            .addVertexBuffer(nvrhi::VertexBufferBinding()
                .setBuffer(m_vb)
                .setSlot(0)
                .setOffset(offsetof(Vertex, Position)))
            .setPipeline(m_pipeline)
            .setFramebuffer(framebuffer)
            .setViewport(nvrhi::ViewportState()
                .addViewportAndScissorRect(nvrhi::Viewport(1280.0f, 720.0f)));

        m_cmdList->setGraphicsState(state);

        nvrhi::DrawArguments args;
        args.setVertexCount(24);
        m_cmdList->drawIndexed(args);

        m_cmdList->close();
        device->executeCommandList(m_cmdList);
    }
    
    bool SupportsDepthBuffer() override { return true; }
    
    void OnBackBufferResizeBegin() override
    {
        m_pipeline = nullptr;
    }
    
    void OnBackBufferResizeEnd(
        [[maybe_unused]] const fay::u32 width,
        [[maybe_unused]] const fay::u32 height,
        [[maybe_unused]] const fay::u32 sampleCount) override { }

private:
    struct Vertex
    {
        SM::Vector3 Position;
        SM::Vector2 UV;
    };

    struct ConstantBufferEntry
    {
        SM::Matrix ViewProjMatrix;
        fay::f32 padding[16 * 3];
    };

    static_assert(sizeof(ConstantBufferEntry) == nvrhi::c_ConstantBufferOffsetSizeAlignment, "sizeof(ConstantBufferEntry) must be 256 bytes");

    static constexpr Vertex g_Vertices[] = 
    {
        { {-0.5f,  0.5f, -0.5f}, {0.0f, 0.0f} }, // front face
        { { 0.5f, -0.5f, -0.5f}, {1.0f, 1.0f} },
        { {-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f} },
        { { 0.5f,  0.5f, -0.5f}, {1.0f, 0.0f} },

        { { 0.5f, -0.5f, -0.5f}, {0.0f, 1.0f} }, // right side face
        { { 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f} },
        { { 0.5f, -0.5f,  0.5f}, {1.0f, 1.0f} },
        { { 0.5f,  0.5f, -0.5f}, {0.0f, 0.0f} },

        { {-0.5f,  0.5f,  0.5f}, {0.0f, 0.0f} }, // left side face
        { {-0.5f, -0.5f, -0.5f}, {1.0f, 1.0f} },
        { {-0.5f, -0.5f,  0.5f}, {0.0f, 1.0f} },
        { {-0.5f,  0.5f, -0.5f}, {1.0f, 0.0f} },

        { { 0.5f,  0.5f,  0.5f}, {0.0f, 0.0f} }, // back face
        { {-0.5f, -0.5f,  0.5f}, {1.0f, 1.0f} },
        { { 0.5f, -0.5f,  0.5f}, {0.0f, 1.0f} },
        { {-0.5f,  0.5f,  0.5f}, {1.0f, 0.0f} },

        { {-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f} }, // top face
        { { 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f} },
        { { 0.5f,  0.5f, -0.5f}, {1.0f, 1.0f} },
        { {-0.5f,  0.5f,  0.5f}, {0.0f, 0.0f} },

        { { 0.5f, -0.5f,  0.5f}, {1.0f, 1.0f} }, // bottom face
        { {-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f} },
        { { 0.5f, -0.5f, -0.5f}, {1.0f, 0.0f} },
        { {-0.5f, -0.5f,  0.5f}, {0.0f, 1.0f} },
    };

    static constexpr uint32_t g_Indices[] =
    {
         0,  2,  1,   0,  1,  3, // front face
         4,  6,  5,   4,  5,  7, // right face
         8, 10,  9,   8,  9, 11, // left face
        12, 14, 13,  12, 13, 15, // back face
        16, 18, 17,  16, 17, 19, // top face
        20, 22, 21,  20, 21, 23, // bottom face
    };
private:
    fay::f32 t = 0.0f;
    nvrhi::CommandListHandle      m_cmdList;
    nvrhi::GraphicsPipelineHandle m_pipeline;
    nvrhi::InputLayoutHandle      m_inputLayout;
    nvrhi::BindingLayoutHandle    m_bindingLayout;
    nvrhi::BindingSetHandle       m_bindingSet;
    nvrhi::ShaderHandle           m_vs;
    nvrhi::ShaderHandle           m_ps;
    nvrhi::BufferHandle           m_cb;
    nvrhi::BufferHandle           m_vb;
    nvrhi::BufferHandle           m_ib;
};

static void Run()
{
    fay::Window window(fay::Window::Desc::Default());

    std::unique_ptr<fay::Renderer> renderer(fay::Renderer::Create(GetPlatformAPI()));

    fay::RendererInitInfo info{};
    info.DepthBufferFormat = nvrhi::Format::D32;
    info.EnableWarningsAsErrors = false;
    info.EnableNVRHIValidationLayer
        = info.EnableDebugRuntime
        = info.EnableGPUValidation = FAY_DEBUG;
    info.LogBufferLifetime = true;

    if (!renderer->Init(info, window))
    {
        fay::Log::Error("Failed to initialize renderer!");
        return;
    }

    ClearPass clearPass(renderer.get(), nvrhi::Color(0.015f));
    DrawPass drawPass(renderer.get());

    renderer->AddRenderPassToBack(&clearPass);
    renderer->AddRenderPassToBack(&drawPass);

    while (window.PumpEvents())
    {
        // TODO: upgrade this to make use of windwo event hooks
        //if (window.HasSizeChanged())
        //{
        //    renderer->Resize();
        //}

        if (renderer->PreRender())
        {
            renderer->DoRenderPasses();
            renderer->PostRender();
        }

        SDL_UpdateWindowSurface(window.GetSDL());

        FrameMark;
    }

    renderer->Shutdown();
}

int main()
{
    fay::App app;
    app.Run();

    //Run();
    return 0;
}

#include "Graphics/RendererBase.h"
#include "Common/Log.h"
#include "Common/Assert.h"
#include "Platform/Window.h"
#include "Common/Profiling.h"
#include <nvrhi/utils.h>

#if FAY_HAS_D3D
#include "Graphics/DX12/RendererDX12.h"
#endif

#if FAY_HAS_VULKAN
#include "Graphics/Vulkan/RendererVulkan.h"
#endif

namespace fay
{
	Renderer* Renderer::Create(nvrhi::GraphicsAPI api)
	{
		ZoneScoped;
		switch (api)
		{
#if FAY_HAS_D3D
		case nvrhi::GraphicsAPI::D3D12: return new RendererDX12();
#endif
#if FAY_HAS_VULKAN
		case nvrhi::GraphicsAPI::VULKAN: return new RendererVulkan();
#endif
		default:
			nvrhi::utils::NotSupported();
			return nullptr;
		}
	}

	bool Renderer::Init(const RendererInitInfo& info, Window& targetWindow)
	{
		ZoneScoped;
		m_window = &targetWindow;
		m_initInfo = info;

		m_initInfo.BackBufferSize = m_window->GetSize();

		if (!CreateInstance())
		{
			Log::Error("Failed to create renderer instance!");
			return false;
		}

		if (!CreateDevice())
		{
			Log::Error("Failed to create renderer device!");
			return false;
		}

		if (!CreateSwapChain())
		{
			Log::Error("Failed to create swap chain!");
			return false;
		}

		// Force a resize event using invalid width and height
		m_initInfo.BackBufferSize = { 0, 0 };

		Resize();

		return true;
	}

	void Renderer::Shutdown()
	{
		ZoneScoped;
		m_swapChainFrameBuffers.clear();
		m_swapChainWithDepthFrameBuffers.clear();
		m_depthBuffer = nullptr;

		DestroyDeviceAndSwapChain();

		m_instanceCreated = false;
	}

	void Renderer::Resize()
	{
		ZoneScoped;
		Assert(m_window);
		auto [width, height] = m_window->GetSize();

		if (width == 0 || height == 0)
		{
			Log::Warn("Tried to resize but window ({}x{}) is minimized", width, height);
			return;
		}

		const auto& [currentWidth, currentHeight] = m_initInfo.BackBufferSize;
		if (currentWidth != width || currentHeight != height)
		{
			BackBufferResizeBegin();

			m_initInfo.BackBufferSize = { width, height };

			ResizeSwapChain();
			BackBufferResizeEnd();
		}
	}

	nvrhi::IFramebuffer* Renderer::GetCurrentFrameBuffer(bool withDepth)
	{
		return GetFrameBuffer(GetCurrentBackBufferIndex(), withDepth);
	}

	nvrhi::IFramebuffer* Renderer::GetFrameBuffer(u32 index, bool withDepth)
	{
		ZoneScoped;
		if (withDepth && index < m_swapChainWithDepthFrameBuffers.size())
		{
			return m_swapChainWithDepthFrameBuffers[index];
		}
		else if (index < m_swapChainFrameBuffers.size())
		{

			return m_swapChainFrameBuffers[index];
		}

		return nullptr;
	}

	bool Renderer::PreRender()
	{
		return BeginFrame();
	}

	void Renderer::DoRenderPasses()
	{
		ZoneScoped;
		for (IRenderPass* pass : m_renderPasses)
		{
			nvrhi::IFramebuffer* buffer = GetCurrentFrameBuffer(pass->SupportsDepthBuffer());
			pass->OnRender(buffer);
		}
	}

	bool Renderer::PostRender()
	{
		ZoneScoped;
		if (!Present())
		{
			Log::Error("Present failed!");
			return false;
		}

		GetDevice()->runGarbageCollection();
		++m_frameIndex;

		return true;
	}

	void Renderer::AddRenderPassToFront(IRenderPass* renderPass)
	{
		ZoneScoped;

		Log::Info("Adding [{}] render pass to front", renderPass->GetName());
		m_renderPasses.remove(renderPass);
		m_renderPasses.push_front(renderPass);

		const auto& [width, height] = m_initInfo.BackBufferSize;
		renderPass->OnBackBufferResizeBegin();
		renderPass->OnBackBufferResizeEnd(width, height, m_initInfo.SwapChainSampleCount);
	}

	void Renderer::AddRenderPassToBack(IRenderPass* renderPass)
	{
		ZoneScoped;

		Log::Info("Adding [{}] render pass to back", renderPass->GetName());
		m_renderPasses.remove(renderPass);
		m_renderPasses.push_back(renderPass);

		const auto& [width, height] = m_initInfo.BackBufferSize;
		renderPass->OnBackBufferResizeBegin();
		renderPass->OnBackBufferResizeEnd(width, height, m_initInfo.SwapChainSampleCount);
	}

	void Renderer::RemoveRenderPass(IRenderPass* renderPass)
	{
		ZoneScoped;
		Log::Info("Removing [{}] render pass", renderPass->GetName());
		m_renderPasses.remove(renderPass);
	}

	bool Renderer::CreateInstance()
	{
		ZoneScoped;
		if (m_instanceCreated)
		{
			return true;
		}
		m_instanceCreated = CreateDeviceIndependentResources();
		return m_instanceCreated;
	}

	void Renderer::BackBufferResizeBegin()
	{
		ZoneScoped;
		// Clear swapchain buffers
		m_swapChainFrameBuffers.clear();
		m_swapChainWithDepthFrameBuffers.clear();

		for (IRenderPass* pass : m_renderPasses)
		{
			pass->OnBackBufferResizeBegin();
		}
	}

	void Renderer::BackBufferResizeEnd()
	{
		ZoneScoped;
		CreateDepthBuffer();

		auto& [width, height] = m_initInfo.BackBufferSize;
		for (IRenderPass* pass : m_renderPasses)
		{
			pass->OnBackBufferResizeEnd(width, height, m_initInfo.SwapChainSampleCount);
		}

		const u32 backBufferCount = GetBackBufferCount();
		m_swapChainFrameBuffers.resize(backBufferCount);
		m_swapChainWithDepthFrameBuffers.resize(backBufferCount);

		for (u32 i = 0; i < backBufferCount; i++)
		{
			nvrhi::FramebufferDesc desc = nvrhi::FramebufferDesc()
				.addColorAttachment(GetBackBuffer(i));

			m_swapChainFrameBuffers[i] = GetDevice()->createFramebuffer(desc);

			if (m_depthBuffer)
			{
				desc.setDepthAttachment(m_depthBuffer);
				m_swapChainWithDepthFrameBuffers[i] = GetDevice()->createFramebuffer(desc);
			}
			else
			{
				m_swapChainWithDepthFrameBuffers[i] = m_swapChainFrameBuffers[i];
			}
		}
	}

	void Renderer::CreateDepthBuffer()
	{
		ZoneScoped;
		m_depthBuffer = nullptr;

		if (m_initInfo.DepthBufferFormat == nvrhi::Format::UNKNOWN)
		{
			Log::Warn("Skipping depth buffer creation, format is unknown");
			return;
		}

		auto& [width, height] = m_initInfo.BackBufferSize;
		const nvrhi::TextureDesc textureDesc = nvrhi::TextureDesc()
			.setDebugName("Depth Buffer")
			.setWidth(width)
			.setHeight(height)
			.setFormat(m_initInfo.DepthBufferFormat)
			.setDimension(m_initInfo.SwapChainSampleCount > 1
				? nvrhi::TextureDimension::Texture2DMS
				: nvrhi::TextureDimension::Texture2D)
			.setSampleCount(m_initInfo.SwapChainSampleCount)
			.setSampleQuality(m_initInfo.SwapChainSampleQuality)
			.setIsTypeless(true)
			.setIsRenderTarget(true)
			.enableAutomaticStateTracking(nvrhi::ResourceStates::DepthWrite);

		m_depthBuffer = GetDevice()->createTexture(textureDesc);
	}

	void Renderer::message(nvrhi::MessageSeverity severity, const char* messageText)
	{
		switch (severity)
		{
			using enum nvrhi::MessageSeverity;
		case Info: Log::Info("[NVRHI] {}", messageText);     break;
		case Warning: Log::Warn("[NVRHI] {}", messageText);  break;
		case Error: Log::Error("[NVRHI] {}", messageText);   break;
		case Fatal: Log::Error("[NVRHI] {}", messageText); Assert(false);  break;
		}
	}
}
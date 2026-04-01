#pragma once
#include "Graphics/RendererBase.h"
#include <unordered_map>
#include <vector>
#include <memory>
#include <imgui.h>

namespace fay
{
	struct ImGuiContext
	{
		nvrhi::DeviceHandle Device;
		nvrhi::CommandListHandle CmdList;

		nvrhi::ShaderHandle VertexShader;
		nvrhi::ShaderHandle PixelShader;
		nvrhi::InputLayoutHandle ShaderAttribLayout;
		
		nvrhi::TextureHandle FontTexture;
		nvrhi::SamplerHandle FontSampler;

		nvrhi::BufferHandle VertexBuffer;
		nvrhi::BufferHandle IndexBuffer;

		nvrhi::BindingLayoutHandle BindingLayout;
		nvrhi::GraphicsPipelineDesc BasePSODesc;

		nvrhi::GraphicsPipelineHandle PSO;
		std::unordered_map<nvrhi::ITexture*, nvrhi::BindingSetHandle> BindingsCache;

		std::vector<ImDrawVert> ImGuiVertexBuffer;
		std::vector<ImDrawIdx> ImGuiIndexBuffer;

		void Init(nvrhi::IDevice* device);
		void OnRender(nvrhi::IFramebuffer* framebuffer);
		void OnResizeBegin();

	private:
		void LoadShaders();
		void CreateInputLayout();
		void CreatePSO();
		bool UpdateFontTexture();
		bool UpdateGeometry(nvrhi::ICommandList* cmdList);
		bool ReallocateBuffer(nvrhi::BufferHandle& buffer, u64 requiredSize, u64 reallocateSize, bool isIndexBuffer);
		nvrhi::IGraphicsPipeline* GetPSO(const nvrhi::FramebufferInfo& info);
		nvrhi::IBindingSet* GetBindingSet(nvrhi::ITexture* texture);
	};

	class ImGuiRenderer : public IRenderPass
	{
	public:
		explicit ImGuiRenderer(Renderer* renderer);
		~ImGuiRenderer();

		void UpdateFrame(f32 deltaTime);

		virtual std::string_view GetName() const override { return "ImGuiRenderer"; }
		virtual void OnRender(nvrhi::IFramebuffer* framebuffer) override;
		virtual bool SupportsDepthBuffer() override { return false; }
		virtual void OnBackBufferResizeEnd(u32, u32, u32) override {}

	protected:
		virtual void OnImGui() = 0;

	protected:
		std::unique_ptr<ImGuiContext> m_ctx;
		bool m_imguiFrameOpened = false;
	};
}
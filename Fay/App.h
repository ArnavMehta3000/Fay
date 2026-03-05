#pragma once
#include <memory>
#include "Platform/Window.h"
#include "Graphics/RendererBase.h"

namespace fay
{
	class Window;

	class App
	{
	public:
		App();
		~App();

		void Run();

	private:
		bool m_initialized;
		Window m_window;
		std::unique_ptr<Renderer> m_renderer;
	};
}
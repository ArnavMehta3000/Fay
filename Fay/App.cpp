#include "App.h"
#include "Common/Profiling.h"
#include "Common/Assert.h"
#include "Common/Log.h"
#include <fstream>
#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

namespace fay
{
	App::App(const App::Desc& desc)
		: m_window(desc.WindowDesc)
		, m_renderer(fay::Renderer::Create(desc.Api))
		, m_scene(std::make_unique<Scene>())
	{
		ZoneScoped;

		m_window.AddEventHook(this);

		InitGraphics();

		m_gltfImporter = std::make_unique<GLTFImporter>(m_renderer.get());

		LoadScene();
		
		Log::Info("Finished App initialization");
	}
	
	App::~App()
	{
		ZoneScoped;

		m_clearPass = nullptr;
		m_geometryPass = nullptr;

		m_scene = nullptr;
		m_gltfImporter = nullptr;

		Log::Info("Destroying application");
		m_renderer->Shutdown();
		m_renderer = nullptr;
	}
	
	void App::Run()
	{
		Log::Info("Running application");

		while (m_window.PumpEvents())
		{
			Update();

			if (m_renderer->PreRender())
			{
				m_renderer->DoRenderPasses();  // Maybe rename this to RenderFrame?
				Assert(m_renderer->PostRender());
			}

			::SDL_UpdateWindowSurface(m_window.GetSDL());

			FrameMark;
		}
	}
	
	void App::OnWindowEvent(const SDL_Event& e)
	{
		switch (e.type)
		{
		case SDL_EVENT_WINDOW_RESIZED:
			m_renderer->Resize();  // Renderer will query the window size internally
			break;
		}
	}
	
	void App::InitGraphics()
	{
		if (!m_renderer->Init(m_desc.RendererInitInfo, m_window))
		{
			Log::Error("Failed to initialize renderer!");
		}

		m_clearPass = std::make_unique<ClearPass>(m_renderer.get(), nvrhi::Color(0.015f));
		m_geometryPass = std::make_unique<GeometryPass>(m_renderer.get());
		
		m_renderer->AddRenderPassToBack(m_clearPass.get());
		m_renderer->AddRenderPassToBack(m_geometryPass.get());
	}

	void App::LoadScene()
	{
		static constexpr auto sceneFile = "Assets/Scenes/Scene.json";
		
		using json = nlohmann::json;

		std::ifstream f(sceneFile);
		json sceneJson = json::parse(f);

		auto meshes = sceneJson["meshes"].get<std::vector<std::string>>();

		
		// Simply load the first mesh as a scene
		u32 collectionIndex = m_scene->AddMeshCollection(m_gltfImporter->Load(meshes[0]));

		// Create mesh node
		const auto* coll = m_scene->GetMeshCollection(collectionIndex);
		for (u32 i = 0; i < static_cast<u32>(coll->Meshes.size()); ++i)
		{
			auto node = std::make_unique<SceneNode>(std::string(coll->Meshes[i]->GetName()));
			node->AddComponent(SceneMeshComponent
			{
				.CollectionIndex = collectionIndex,
				.MeshIndex = i
			});
			m_scene->GetRoot()->AddChild(std::move(node));
		}
	}
	
	void App::Update()
	{
		m_camera.Transform.Position = { 0.0f, 0.0f, -15.0f };
		m_camera.LookAt(SM::Vector3::Zero);

		m_scene->UpdateTransforms();
		m_geometryPass->SetFrameData(m_scene.get(), m_camera);

	}
}
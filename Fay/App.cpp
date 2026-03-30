#include "App.h"
#include "Common/Profiling.h"
#include "Common/Assert.h"
#include "Common/Log.h"
#include "Platform/Timer.h"
#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

import std;

namespace fay
{
	Timer g_timer("Fay Global Timer");

	App::App(const App::Desc& desc)
		: m_window(desc.WindowDesc)
		, m_renderer(fay::Renderer::Create())
		, m_scene(std::make_unique<Scene>())
		, m_cameraController(m_camera, m_window)
	{
		ZoneScoped;

		m_window.AddEventHook(this);
		m_window.AddEventHook(&m_cameraController);

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
		g_timer.Reset();

		while (m_window.PumpEvents())
		{
			const f32 dt = g_timer.Tick();
			g_timer.UpdateFPSCounter();

			Update(dt);

			if (m_renderer->PreRender())
			{
				m_renderer->DoRenderPasses();

				[[maybe_unused]] bool postRenderResult = m_renderer->PostRender();
				Assert(postRenderResult);
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

		case SDL_EVENT_KEY_DOWN:
			if (e.key.scancode == SDL_SCANCODE_F)  // Print fps info
			{
				Log::Info("FPS: {} | Frame Count: {}", g_timer.FPS(), g_timer.FrameCount());
			}

			if (e.key.scancode == SDL_SCANCODE_P)  // Toggle wireframe
			{
				using enum nvrhi::RasterFillMode;
				m_geometryPass->SetFillMode((m_geometryPass->GetFillMode() == Fill) ? Wireframe : Fill);
			}

			if (e.key.scancode == SDL_SCANCODE_SPACE)  // Reset camera to orit around the root
			{
				const Transform& target = m_scene->GetRoot()->GetLocalTransform();
				m_cameraController.FrameTarget(target.GetPosition());
			}
			break;
		}
	}
	
	void App::InitGraphics()
	{
		ZoneScoped;

		if (!m_renderer->Init(m_desc.RendererInitInfo, m_window))
		{
			Log::Error("Failed to initialize renderer!");
			Assert(false);
		}

		m_clearPass = std::make_unique<ClearPass>(m_renderer.get(), nvrhi::Color(0.015f));
		m_geometryPass = std::make_unique<GeometryPass>(m_renderer.get());
		
		m_renderer->AddRenderPassToBack(m_clearPass.get());
		m_renderer->AddRenderPassToBack(m_geometryPass.get());
	}

	void App::LoadScene()
	{
		ZoneScoped;

		using json = nlohmann::json;
		
		static constexpr auto sceneFile = "Assets/Scenes/Scene.json";

		std::ifstream f(sceneFile);
		json sceneJson = json::parse(f);

		auto meshes = sceneJson["meshes"].get<std::vector<std::string>>();

		// Load all meshes
		for (const std::string& meshStr : meshes)
		{
			std::unique_ptr<MeshCollection> loadedCollection = m_gltfImporter->Load(meshStr);
			const MeshCollection* collection = loadedCollection.get();

			const u32 collectionIdx = m_scene->AddMeshCollection(std::move(loadedCollection));

			// Create a node for each mesh in the collection
			for (u32 i = 0; i < static_cast<u32>(collection->Meshes.size()); ++i)
			{
				auto node = std::make_unique<SceneNode>(std::string(collection->Meshes[i]->GetName()));
				node->AddComponent(SceneMeshComponent{ .CollectionIndex = collectionIdx, .MeshIndex = i });

				m_scene->GetRoot()->AddChild(std::move(node));
			}
		}

		m_cameraController.FrameTarget(m_scene->GetRoot()->GetLocalTransform().GetPosition());
		m_scene->PrintSceneTree();
	}
	
	void App::Update([[maybe_unused]] const f32 dt)
	{
		ZoneScoped;
		
		m_scene->UpdateTransforms();
		
		m_cameraController.Update(dt);

		m_geometryPass->SetFrameData(m_scene.get(), m_camera);
	}
}
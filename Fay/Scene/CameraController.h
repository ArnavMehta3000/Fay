#pragma once
#include "Common/Types.h"
#include "Platform/IWindowEventHook.h"
#include <SimpleMath.h>
#include <bitset>

namespace fay
{
	class Camera;
	class Window;

	namespace SM = DirectX::SimpleMath;

	class CameraController : public IWindowEventHook
	{
	public:
		enum class Mode : u8
		{
			Orbit,  // Left-drag rotates around focus point
			Fly,    // Right-drag free-look, WASD movement
			Pan,    // Middle-drag translates on camera plane
			None,
		};

		struct Settings
		{
			f32 OrbitSensitivity    = 0.005f;  // rad / pixel
			f32 PanSensitivity      = 0.005f;  // world units / pixel
			f32 ZoomSensitivity     = 0.15f;   // multiplier per scroll tick
			f32 FlySpeed            = 5.0f;    // world units / second
			f32 FlySprintMultiplier = 3.0f;    // shift-held multiplier
			f32 FreeLookSensitivity = 0.003f;  // rad / pixel
			f32 MinOrbitDistance    = 0.1f;
			f32 MaxOrbitDistance    = 500.0f;
			f32 ZoomDampening       = 0.85f;   // smooth zoom decay [0..1)
			f32 MovementSmoothing   = 10.0f;   // lerp factor for velocity damping
		};

	public:
		explicit CameraController(Camera& camera, Window& window);

		void OnWindowEvent(const SDL_Event& e) override;
		void Update(f32 dt);

		void SetFocusPoint(const SM::Vector3& point);
		void SetOrbitDistance(f32 distance);
		void FrameTarget(const SM::Vector3& target, f32 distance = 5.0f);

		[[nodiscard]] inline Settings& GetSettings()             { return m_settings;      }
		[[nodiscard]] inline Mode GetActiveMode() const          { return m_activeMode;    }
		[[nodiscard]] inline const Settings& GetSettings() const { return m_settings;      }
		[[nodiscard]] inline SM::Vector3  GetFocusPoint() const  { return m_focusPoint;    }
		[[nodiscard]] inline f32 GetOrbitDistance() const        { return m_orbitDistance; }

	private:
		void InitializeFromCamera();

		void HandleMouseButtonDown(const SDL_Event& e);
		void HandleMouseButtonUp(const SDL_Event& e);
		void HandleMouseMotion(const SDL_Event& e);
		void HandleMouseWheel(const SDL_Event& e);
		void HandleKeyDown(const SDL_Event& e);
		void HandleKeyUp(const SDL_Event& e);

		void ApplyOrbitMotion(f32 dx, f32 dy);
		void ApplyPanMotion(f32 dx, f32 dy);
		void ApplyFreeLookMotion(f32 dx, f32 dy);

		void UpdateFlyMovement(f32 dt);

		void RecalculateOrbitFromCamera();
		void ApplyOrbitToCamera();
		void ClampPitch();

	private:
		static constexpr std::size_t KEY_COUNT = 512;

		Settings               m_settings;
		Camera*                m_camera        = nullptr;
		Window*                m_window        = nullptr;
		Mode                   m_activeMode    = Mode::None;
		bool                   m_initialized   = false;
		SM::Vector3            m_focusPoint    = SM::Vector3::Zero;
		f32                    m_orbitDistance = 5.0f;
		f32                    m_yaw           = 0.0f;
		f32                    m_pitch         = 0.0f;
		f32                    m_zoomVelocity  = 0.0f;
		SM::Vector3            m_flyVelocity   = SM::Vector3::Zero;
		f32                    m_lastMouseX    = 0.0f;
		f32                    m_lastMouseY    = 0.0f;
		std::bitset<KEY_COUNT> m_keysDown{};
	};

	inline std::string_view ToString(CameraController::Mode mode) noexcept
	{
		switch (mode)
		{
			case CameraController::Mode::Orbit:  return "Orbit";
			case CameraController::Mode::Fly:    return "Fly";
			case CameraController::Mode::Pan:    return "Pan";
			default:                             return "None";
		}
	}
}
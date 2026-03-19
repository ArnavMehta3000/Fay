#include "Scene/CameraController.h"
#include "Platform/Window.h"
#include "Graphics/Camera.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <numbers>

namespace fay
{
	CameraController::CameraController(Camera& camera, Window& window)
		: m_camera(&camera)
		, m_window(&window)
	{
		RecalculateOrbitFromCamera();
	}

	void CameraController::OnWindowEvent(const SDL_Event& e)
	{
		switch (e.type)
		{
		case SDL_EVENT_MOUSE_BUTTON_DOWN: HandleMouseButtonDown(e); break;
		case SDL_EVENT_MOUSE_BUTTON_UP:   HandleMouseButtonUp(e);   break;
		case SDL_EVENT_MOUSE_MOTION:      HandleMouseMotion(e);     break;
		case SDL_EVENT_MOUSE_WHEEL:       HandleMouseWheel(e);      break;
		case SDL_EVENT_KEY_DOWN:          HandleKeyDown(e);         break;
		case SDL_EVENT_KEY_UP:            HandleKeyUp(e);           break;
		default: break;
		}
	}
	
	void CameraController::Update(f32 dt)
	{
		// Smooth zoom / orbit dolly
		if (std::abs(m_zoomVelocity) > 1e-4f)
		{
			m_orbitDistance += m_zoomVelocity * dt;
			m_orbitDistance = std::clamp(m_orbitDistance, m_settings.MinOrbitDistance, m_settings.MaxOrbitDistance);
			m_zoomVelocity *= m_settings.ZoomDampening;

			// Only apply orbit positioning when not in fly mode
			if (m_activeMode != Mode::Fly)
			{
				ApplyOrbitToCamera();
			}
		}

		UpdateFlyMovement(dt);
	}
	
	void CameraController::SetFocusPoint(const SM::Vector3& point)
	{
		m_focusPoint = point;
		RecalculateOrbitFromCamera();
	}
	
	void CameraController::SetOrbitDistance(f32 distance)
	{
		m_orbitDistance = std::clamp(distance, m_settings.MinOrbitDistance, m_settings.MaxOrbitDistance);
		ApplyOrbitToCamera();
	}
	
	void CameraController::FrameTarget(const SM::Vector3& target, f32 distance)
	{
		m_focusPoint = target;
		m_orbitDistance = std::clamp(distance, m_settings.MinOrbitDistance, m_settings.MaxOrbitDistance);
		ApplyOrbitToCamera();
	}
	
	void CameraController::HandleMouseButtonDown(const SDL_Event& e)
	{
		m_lastMouseX = e.button.x;
		m_lastMouseY = e.button.y;

		switch (e.button.button)
		{
		case SDL_BUTTON_LEFT:
			if (m_activeMode == Mode::None)
				m_activeMode = Mode::Orbit;
			break;

		case SDL_BUTTON_RIGHT:
			if (m_activeMode == Mode::None)
			{
				m_activeMode = Mode::Fly;
				// Sync yaw/pitch from current camera orientation so there is no snap when entering fly mode.
				RecalculateOrbitFromCamera();
				SDL_SetWindowRelativeMouseMode(m_window->GetSDL(), true);
			}
			break;

		case SDL_BUTTON_MIDDLE:
			if (m_activeMode == Mode::None)
				m_activeMode = Mode::Pan;
			break;

		default: break;
		}
	}
	
	void CameraController::HandleMouseButtonUp(const SDL_Event& e)
	{
		auto modeForButton = [](u8 button) -> Mode
			{
				switch (button)
				{
				case SDL_BUTTON_LEFT:   return Mode::Orbit;
				case SDL_BUTTON_RIGHT:  return Mode::Fly;
				case SDL_BUTTON_MIDDLE: return Mode::Pan;
				default:                return Mode::None;
				}
			};

		if (modeForButton(e.button.button) == m_activeMode)
		{
			if (m_activeMode == Mode::Fly)
			{
				SDL_SetWindowRelativeMouseMode(m_window->GetSDL(), false);
				// Sync orbit state back from the free-look position
				m_focusPoint = m_camera->Transform.GetPosition()
					+ m_camera->Transform.Forward() * m_orbitDistance;
			}

			m_activeMode = Mode::None;
			m_flyVelocity = SM::Vector3::Zero;
		}
	}
	
	void CameraController::HandleMouseMotion(const SDL_Event& e)
	{
		const f32 dx = e.motion.xrel;
		const f32 dy = e.motion.yrel;

		switch (m_activeMode)
		{
		case Mode::Orbit: ApplyOrbitMotion(dx, dy);     break;
		case Mode::Pan:   ApplyPanMotion(dx, dy);       break;
		case Mode::Fly:   ApplyFreeLookMotion(dx, dy);  break;
		default: break;
		}

		m_lastMouseX = e.motion.x;
		m_lastMouseY = e.motion.y;
	}
	
	void CameraController::HandleMouseWheel(const SDL_Event& e)
	{
		const f32 scroll = e.wheel.y;

		if (m_activeMode == Mode::Fly)
		{
			// Adjust fly speed with scroll while in fly mode
			m_settings.FlySpeed *= (1.0f + scroll * m_settings.ZoomSensitivity);
			m_settings.FlySpeed = std::clamp(m_settings.FlySpeed, 0.1f, 200.0f);
		}
		else
		{
			// Dolly zoom — velocity-based for smoothness
			m_zoomVelocity -= scroll * m_orbitDistance * m_settings.ZoomSensitivity;
		}
	}
	
	void CameraController::HandleKeyDown(const SDL_Event& e)
	{
		if (e.key.scancode < KEY_COUNT)
		{
			m_keysDown.set(static_cast<std::size_t>(e.key.scancode));
		}
	}
	
	void CameraController::HandleKeyUp(const SDL_Event& e)
	{
		if (e.key.scancode < KEY_COUNT)
		{
			m_keysDown.reset(static_cast<std::size_t>(e.key.scancode));
		}
	}
	
	void CameraController::ApplyOrbitMotion(f32 dx, f32 dy)
	{
		m_yaw   -= dx * m_settings.OrbitSensitivity;
		m_pitch -= dy * m_settings.OrbitSensitivity;
		
		ClampPitch();
		ApplyOrbitToCamera();
	}
	
	void CameraController::ApplyPanMotion(f32 dx, f32 dy)
	{
		// Scale pan speed by distance so it feels consistent
		const f32 scale = m_orbitDistance * m_settings.PanSensitivity;

		const SM::Vector3 right = m_camera->Transform.Right();
		const SM::Vector3 up = m_camera->Transform.Up();

		const SM::Vector3 offset = (-right * dx + up * dy) * scale;

		m_focusPoint += offset;
		m_camera->Transform.Translate(offset);
	}
	
	void CameraController::ApplyFreeLookMotion(f32 dx, f32 dy)
	{
		m_yaw -= dx * m_settings.FreeLookSensitivity;
		m_pitch -= dy * m_settings.FreeLookSensitivity;
		ClampPitch();

		// Build rotation from yaw/pitch and apply directly
		const SM::Quaternion qYaw   = SM::Quaternion::CreateFromAxisAngle(SM::Vector3::Up, m_yaw);
		const SM::Quaternion qPitch = SM::Quaternion::CreateFromAxisAngle(SM::Vector3::Right, m_pitch);

		m_camera->Transform.SetRotation(qPitch * qYaw);
	}
	
	void CameraController::UpdateFlyMovement(f32 dt)
	{
		if (m_activeMode != Mode::Fly)
		{
			return;
		}

		auto isDown = [this](SDL_Scancode key) -> bool
		{
			return m_keysDown.test(static_cast<std::size_t>(key));
		};

		SM::Vector3 wishDir = SM::Vector3::Zero;

		if (isDown(SDL_SCANCODE_W)) wishDir += m_camera->Transform.Forward();
		if (isDown(SDL_SCANCODE_S)) wishDir -= m_camera->Transform.Forward();
		if (isDown(SDL_SCANCODE_D)) wishDir += m_camera->Transform.Right();
		if (isDown(SDL_SCANCODE_A)) wishDir -= m_camera->Transform.Right();
		if (isDown(SDL_SCANCODE_E)) wishDir += SM::Vector3::Up;
		if (isDown(SDL_SCANCODE_Q)) wishDir -= SM::Vector3::Up;

		if (wishDir.LengthSquared() > 1e-6f)
		{
			wishDir.Normalize();
		}

		f32 speed = m_settings.FlySpeed;
		if (isDown(SDL_SCANCODE_LSHIFT))
		{
			speed *= m_settings.FlySprintMultiplier;
		}

		// Smooth velocity towards target
		const SM::Vector3 targetVel = wishDir * speed;
		m_flyVelocity = SM::Vector3::Lerp(m_flyVelocity, targetVel, std::min(1.0f, m_settings.MovementSmoothing * dt));

		m_camera->Transform.Translate(m_flyVelocity * dt);
	}
	
	void CameraController::RecalculateOrbitFromCamera()
	{
		// Derive yaw/pitch from the camera's current orientation
		const SM::Vector3 fwd = m_camera->Transform.Forward();

		m_yaw   = std::atan2(fwd.x, fwd.z);                  // heading
		m_pitch = std::asin(std::clamp(fwd.y, -1.0f, 1.0f)); // elevation

		// Place focus point in front of the camera at the current orbit distance
		m_focusPoint = m_camera->Transform.GetPosition() + fwd * m_orbitDistance;
	}
	
	void CameraController::ApplyOrbitToCamera()
	{
		// Spherical → Cartesian offset from focus point
		const f32 cp = std::cos(m_pitch);
		const SM::Vector3 offset
		{
			 cp * std::sin(m_yaw) * (-m_orbitDistance),
			 std::sin(m_pitch) * (-m_orbitDistance),
			 cp * std::cos(m_yaw) * (-m_orbitDistance)
		};

		m_camera->Transform.SetPosition(m_focusPoint + offset);
		m_camera->LookAt(m_focusPoint);
	}
	
	void CameraController::ClampPitch()
	{
		constexpr f32 kLimit = std::numbers::pi_v<f32> / 2.0f - 0.01f;
		m_pitch = std::clamp(m_pitch, -kLimit, kLimit);
	}
}
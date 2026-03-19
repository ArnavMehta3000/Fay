#pragma once
#include "Components/Transform.h"
#include "Common/Types.h"

namespace fay
{
	class Camera
	{
	public:
		struct Film
		{
			f32 Width       = 36.0f;   // mm
			f32 height      = 24.0f;   // mm
			f32 AspectRatio = 16.0f / 9.0f;
		};

		struct Lens
		{
			f32 FocalLength = 50.0f;  // mm
			f32 Aperture    = 1.8f;   // F-Stops
			f32 FocusDist   = 10.0f;  // meteres

			[[nodiscard]] f32 GetFOV(f32 filmWidth) const;
			[[nodiscard]] f32 ApertureRadius() const;
		};

		struct Projection
		{
			f32 NearPlane = 0.01f;
			f32 FarPlane = 1000.0f;

			[[nodiscard]] SM::Matrix Perspective(f32 fov, f32 aspect) const;
		};

		// UBO struct for shaders
		struct RenderData
		{
			SM::Matrix View;
			SM::Matrix Projection;
			SM::Matrix ViewProjection;

			SM::Vector3 CameraPosition;
			f32 NearPlane;
			f32 FarPlane;
			
			f32 ApertureRadius;
			f32 FocusDistance;

			// Padding to take it to 256 byte boundary
			f32 _padding[9];
		};

		static_assert(sizeof(RenderData) % 256 == 0, "RenderData must be a multiple of 256 bytes");

	public:
		[[nodiscard]] SM::Matrix GetViewMatrix() const;
		[[nodiscard]] SM::Matrix GetProjectionMatrix() const;
		[[nodiscard]] SM::Matrix GetViewProjectionMatrix() const;

		void SetAspectRatio(f32 width, f32 height);
		void LookAt(const SM::Vector3 target);

	public:
		Transform Transform;
		Film Film;
		Lens Lens;
		Projection Projection;
	};
}
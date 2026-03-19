#include "Graphics/Camera.h"
#undef near
#undef far

namespace fay
{	
	f32 Camera::Lens::GetFOV(f32 filmWidth) const
	{
		// PBRT FOV calculation
		return 2.0f * std::atan(filmWidth / (2.0f * FocalLength));
	}
	
	f32 Camera::Lens::ApertureRadius() const
	{
		return FocalLength / (2.0f * Aperture);
	}
	
	SM::Matrix Camera::Projection::Perspective(f32 fov, f32 aspect) const
	{
		return SM::Matrix::CreatePerspectiveFieldOfView(fov, aspect, NearPlane, FarPlane);
	}


	SM::Matrix Camera::GetViewMatrix() const
	{
		SM::Matrix rot = SM::Matrix::CreateFromQuaternion(Transform.GetRotation());
		SM::Matrix trans = SM::Matrix::CreateTranslation(Transform.GetPosition());
		return (rot * trans).Invert();
	}

	SM::Matrix Camera::GetProjectionMatrix() const
	{
		const f32 fov = Lens.GetFOV(Film.Width);
		return Projection.Perspective(fov, Film.AspectRatio);
	}

	SM::Matrix Camera::GetViewProjectionMatrix() const
	{
		return GetViewMatrix() * GetProjectionMatrix();
	}

	void Camera::SetAspectRatio(f32 width, f32 height)
	{
		Film.AspectRatio = width / height;
	}

	void Camera::LookAt(const SM::Vector3 target)
	{
		SM::Vector3 forward = target - Transform.GetPosition();
		forward.Normalize();

		Transform.SetRotation(
			SM::Quaternion::CreateFromRotationMatrix(
				SM::Matrix::CreateWorld(Transform.GetPosition(), forward, SM::Vector3::Up)
			)
		);
	}
}
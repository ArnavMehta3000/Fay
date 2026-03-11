#include "Graphics/Camera.h"
#undef near
#undef far

namespace fay
{
	SM::Matrix Camera::Transform::GetViewMatrix() const
	{
		SM::Matrix rot = SM::Matrix::CreateFromQuaternion(Rotation);
		SM::Matrix trans = SM::Matrix::CreateTranslation(Position);

		return (rot * trans).Invert();
	}
	
	SM::Vector3 Camera::Transform::Forward() const
	{
		return SM::Vector3::Transform(SM::Vector3::Forward, Rotation);
	}
	
	SM::Vector3 Camera::Transform::Right() const
	{
		return SM::Vector3::Transform(SM::Vector3::Right, Rotation);
	}
	
	SM::Vector3 Camera::Transform::Up() const
	{
		return SM::Vector3::Transform(SM::Vector3::Up, Rotation);
	}
	
	
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
		return Transform.GetViewMatrix();
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
		SM::Vector3 forward = (target - Transform.Position);
		forward.Normalize();

		Transform.Rotation = SM::Quaternion::CreateFromRotationMatrix(
			SM::Matrix::CreateWorld(
				Transform.Position,
				forward,
				SM::Vector3::Up)
		);
	}
}
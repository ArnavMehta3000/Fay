#include "Components/Transform.h"

namespace fay
{
	Transform::Transform(SM::Vector3 position, SM::Quaternion rotation, SM::Vector3 scale)
		: m_position(position)
		, m_rotation(rotation)
		, m_scale(scale)
	{
		m_rotation.Normalize();
		MarkDirty();
	}
	
	void Transform::SetPosition(const SM::Vector3& pos)
	{
		m_position = pos;
		MarkDirty();
	}
	
	void Transform::SetRotation(const SM::Quaternion& rot)
	{
		m_rotation = rot;
		m_rotation.Normalize();
		MarkDirty();
	}
	
	void Transform::SetScale(const SM::Vector3& scale)
	{
		m_scale = scale;
		MarkDirty();
	}
	
	void Transform::Translate(const SM::Vector3& delta)
	{
		m_position += delta;
		MarkDirty();
	}
	
	void Transform::Rotate(const SM::Quaternion& delta)
	{
		m_rotation *= delta;
		m_rotation.Normalize();
		MarkDirty();
	}
	
	void Transform::SetEulerRotation(SM::Vector3 euler)
	{
		m_rotation = SM::Quaternion::CreateFromYawPitchRoll(euler.y, euler.x, euler.z);
		MarkDirty();
	}
	
	const SM::Matrix& Transform::ToLocalMatrix() const
	{
		if (m_dirty)
		{
			RecalculateMatrix();
		}

		return m_localMatrix;
	}


	SM::Vector3 Transform::Forward() const
	{
		return SM::Vector3::Transform(SM::Vector3::Forward, m_rotation);
	}
	
	SM::Vector3 Transform::Right() const
	{
		return SM::Vector3::Transform(SM::Vector3::Right, m_rotation);
	}
	
	SM::Vector3 Transform::Up() const
	{
		return SM::Vector3::Transform(SM::Vector3::Up, m_rotation);
	}

	void Transform::RecalculateMatrix() const
	{
		SM::Matrix scale = SM::Matrix::CreateScale(m_scale);
		SM::Matrix rot   = SM::Matrix::CreateFromQuaternion(m_rotation);
		SM::Matrix trans = SM::Matrix::CreateTranslation(m_position);

		m_localMatrix = scale * rot * trans;
		m_dirty = false;
	}
}
#pragma once
#include <SimpleMath.h>

namespace fay
{
	namespace SM = DirectX::SimpleMath;

	class Transform
	{
	public:
		Transform(SM::Vector3 position,
			SM::Quaternion rotation = SM::Quaternion::Identity,
			SM::Vector3 scale = SM::Vector3::One);

		void SetPosition(const SM::Vector3& pos);
		void SetRotation(const SM::Quaternion& rot);
		void SetScale(const SM::Vector3& scale);

		[[nodiscard]] const SM::Vector3& GetPosition() const { return m_position; }
		[[nodiscard]] const SM::Quaternion& GetRotation() const { return m_rotation; }
		[[nodiscard]] const SM::Vector3& GetScale() const { return m_scale; }

		void Translate(const SM::Vector3& delta);
		void Rotate(const SM::Quaternion& delta);
		void SetEulerRotation(SM::Vector3 euler);

		[[nodiscard]] const SM::Matrix& ToLocalMatrix() const;
		[[nodiscard]] SM::Vector3 Forward() const;
		[[nodiscard]] SM::Vector3 Right() const;
		[[nodiscard]] SM::Vector3 Up() const;

	private:
		inline void MarkDirty() { m_dirty = true; }
		void RecalculateMatrix() const;

	private:
		SM::Vector3        m_position    = SM::Vector3::Zero;
		SM::Quaternion     m_rotation    = SM::Quaternion::Identity;
		SM::Vector3        m_scale       = SM::Vector3::One;
		mutable SM::Matrix m_localMatrix = SM::Matrix::Identity;
		mutable bool       m_dirty       = true;
	};
}
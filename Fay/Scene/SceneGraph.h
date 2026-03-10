#pragma once
#include <functional>
#include <memory>
#include <unordered_map>
#include <any>
#include <typeindex>
#include "Graphics/Mesh.h"

namespace fay
{
	class SceneNode;
	class Scene;

	class SceneNode
	{
		using ComponentMap = std::unordered_map<std::type_index, std::any>;
		using Children = std::vector<std::unique_ptr<SceneNode>>;

	public:
		explicit SceneNode(const std::string& name = "Node") : m_name(name) {}

		SceneNode* AddChild(std::unique_ptr<SceneNode> child);

		[[nodiscard]] inline SceneNode* GetParent() const { return m_parent; }
		[[nodiscard]] inline const Children& GetChildren() const { return m_children; }
		[[nodiscard]] inline std::string_view GetName() const { return m_name; }

		template<typename T>
		T& AddComponent(T component = {})
		{
			auto [it, _] = m_components.emplace(std::type_index(typeid(T)), std::make_any<T>(std::move(component)));
			return std::any_cast<T&>(it->second);
		}

		template<typename T>
		[[nodiscard]] T* GetComponent()
		{
			auto it = m_components.find(std::type_index(typeid(T)));
			return (it != m_components.end()) ? std::any_cast<T>(&it->second) : nullptr;
		}

		template<typename T>
		[[nodiscard]] const T* GetComponent() const
		{
			auto it = m_components.find(std::type_index(typeid(T)));
			return (it != m_components.end()) ? std::any_cast<const T>(&it->second) : nullptr;
		}

		template<typename T>
		[[nodiscard]] bool HasComponent() const
		{
			return m_components.contains(std::type_index(typeid(T)));
		}

	public:
		Transform LocalTransform;
		SM::Matrix WorldMatrix = SM::Matrix::Identity;

	private:
		std::string  m_name;
		SceneNode*   m_parent = nullptr;
		Children     m_children;
		ComponentMap m_components;
	};


	class Scene
	{
	public:
		using MeshVisitor   = std::function<void(const SceneNode&, const Mesh&, const SM::Matrix&)>;
		using CameraVisitor = std::function<void(const SceneNode&, const CameraComponent&, const SM::Matrix&)>;
		using LightVisitor  = std::function<void(const SceneNode&, const LightComponent&, const SM::Matrix&)>;

		Scene();
		~Scene();		

		Scene(const Scene&) = delete;
		Scene& operator=(const Scene&) = delete;
		Scene(Scene&&) noexcept = default;
		Scene& operator=(Scene&&) noexcept = default;

		[[nodiscard]] inline SceneNode* GetRoot() { return m_root.get(); }
		[[nodiscard]] inline const SceneNode* GetRoot() const { return m_root.get(); }

		inline void UpdateTransforms()                               { TraverseRecursive(m_root.get(), SM::Matrix::Identity); }
		inline void ForEachMeshNode(const MeshVisitor& fn) const     { ForEachMeshNodeRecursive(m_root.get(), fn);            }
		inline void ForEachCameraNode(const CameraVisitor& fn) const { ForEachCameraNodeRecursive(m_root.get(), fn);          }
		inline void ForEachLightNode(const LightVisitor& fn) const   { ForEachLightNodeRecursive(m_root.get(), fn);           }

	private:
		static void TraverseRecursive(SceneNode* node, const SM::Matrix& parentWorld);
		void ForEachMeshNodeRecursive(const SceneNode* node, const MeshVisitor& fn) const;
		void ForEachCameraNodeRecursive(const SceneNode* node, const CameraVisitor& fn) const;
		void ForEachLightNodeRecursive(const SceneNode* node, const LightVisitor& fn) const;

	public:
		std::vector<std::unique_ptr<Mesh>> Meshes;
		std::vector<std::unique_ptr<Material>> Materials;

	private:
		std::unique_ptr<SceneNode> m_root;
	};
}
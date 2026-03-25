#pragma once
#include <functional>
#include <memory>
#include <unordered_map>
#include <any>
#include <typeindex>
#include <sstream>
#include "Components/Transform.h"
#include "Graphics/Mesh.h"
#include "Scene/GLTFImporter.h"

namespace fay
{
	class SceneNode;
	class Scene;

	class SceneNode
	{
		using ComponentMap = std::unordered_map<std::type_index, std::any>;
		using Children = std::vector<std::unique_ptr<SceneNode>>;

	public:
		explicit SceneNode(const std::string& name = "Node") : m_name(name), m_localTransform(SM::Vector3::Zero) {}

		SceneNode* AddChild(std::unique_ptr<SceneNode> child);

		[[nodiscard]] inline SceneNode* GetParent() const { return m_parent; }
		[[nodiscard]] inline const Children& GetChildren() const { return m_children; }
		[[nodiscard]] inline std::string_view GetName() const { return m_name; }
		[[nodiscard]] inline bool HasChildren() const { return !m_children.empty(); }
		[[nodiscard]] inline bool HasAnyComponent() const { return !m_components.empty(); }

		[[nodiscard]] inline Transform& GetLocalTransform() { return m_localTransform; }
		[[nodiscard]] inline const Transform& GetLocalTransform() const { return m_localTransform; }

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
		SM::Matrix WorldMatrix = SM::Matrix::Identity;

	private:
		std::string  m_name;
		Transform    m_localTransform;
		SceneNode*   m_parent = nullptr;
		Children     m_children;
		ComponentMap m_components;
	};

	// MeshComponent now references a specific MeshCollection (by index into Scene::m_meshCollections)
	// and a mesh within that collection.
	struct SceneMeshComponent
	{
		u32 CollectionIndex = 0;  // index into Scene::m_meshCollections
		u32 MeshIndex = 0;       // index into MeshCollection::Meshes
	};

	class Scene
	{
	public:
		using MeshVisitor = std::function<void(const SceneNode&, const Mesh&, const SM::Matrix&)>;

		Scene();
		~Scene();

		Scene(const Scene&) = delete;
		Scene& operator=(const Scene&) = delete;
		Scene(Scene&&) noexcept = default;
		Scene& operator=(Scene&&) noexcept = default;

		[[nodiscard]] inline SceneNode* GetRoot() { return m_root.get(); }
		[[nodiscard]] inline const SceneNode* GetRoot() const { return m_root.get(); }

		u32 AddMeshCollection(std::unique_ptr<MeshCollection> collection);

		[[nodiscard]] inline const std::vector<std::unique_ptr<MeshCollection>>& GetMeshCollections() const { return m_meshCollections; }
		[[nodiscard]] const MeshCollection* GetMeshCollection(u32 index) const;

		[[nodiscard]] const Mesh* ResolveMesh(const SceneMeshComponent& comp) const;
		[[nodiscard]] const std::vector<std::unique_ptr<Material>>* ResolveMaterials(const SceneMeshComponent& comp) const;

		inline void UpdateTransforms() { TraverseRecursive(m_root.get(), SM::Matrix::Identity); }
		inline void ForEachMeshNode(const MeshVisitor& fn) const { ForEachMeshNodeRecursive(m_root.get(), fn); }

		void PrintSceneTree();

	private:
		void PrintSceneTreeInternal(std::stringstream& out, const SceneNode* node);
		static void TraverseRecursive(SceneNode* node, const SM::Matrix& parentWorld);
		void ForEachMeshNodeRecursive(const SceneNode* node, const MeshVisitor& fn) const;

	private:
		std::unique_ptr<SceneNode> m_root;
		std::vector<std::unique_ptr<MeshCollection>> m_meshCollections;
	};
}
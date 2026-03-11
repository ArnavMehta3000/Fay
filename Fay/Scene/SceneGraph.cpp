#include "Scene/SceneGraph.h"

namespace fay
{
	SceneNode* SceneNode::AddChild(std::unique_ptr<SceneNode> child)
	{
		child->m_parent = this;
		m_children.push_back(std::move(child));
		return m_children.back().get();
	}

	Scene::Scene()
		: m_root(std::make_unique<SceneNode>("Root"))
	{
	}

	Scene::~Scene()
	{
		m_root = nullptr;
		m_meshCollections.clear();
	}

	u32 Scene::AddMeshCollection(std::unique_ptr<MeshCollection> collection)
	{
		u32 index = static_cast<u32>(m_meshCollections.size());
		m_meshCollections.push_back(std::move(collection));
		return index;
	}

	const MeshCollection* Scene::GetMeshCollection(u32 index) const
	{
		if (index < m_meshCollections.size())
			return m_meshCollections[index].get();
		return nullptr;
	}

	const Mesh* Scene::ResolveMesh(const SceneMeshComponent& comp) const
	{
		if (comp.CollectionIndex < m_meshCollections.size())
		{
			auto& collection = m_meshCollections[comp.CollectionIndex];
			if (comp.MeshIndex < collection->Meshes.size())
				return collection->Meshes[comp.MeshIndex].get();
		}
		return nullptr;
	}

	const std::vector<std::unique_ptr<Material>>* Scene::ResolveMaterials(const SceneMeshComponent& comp) const
	{
		if (comp.CollectionIndex < m_meshCollections.size())
			return &m_meshCollections[comp.CollectionIndex]->Materials;
		return nullptr;
	}

	void Scene::TraverseRecursive(SceneNode* node, const SM::Matrix& parentWorld)
	{
		node->WorldMatrix = node->GetLocalTransform().ToLocalMatrix() * parentWorld;

		const auto& children = node->GetChildren();
		for (auto& child : children)
		{
			TraverseRecursive(child.get(), node->WorldMatrix);
		}
	}

	void Scene::ForEachMeshNodeRecursive(const SceneNode* node, const MeshVisitor& fn) const
	{
		if (auto* mc = node->GetComponent<SceneMeshComponent>())
		{
			if (const Mesh* mesh = ResolveMesh(*mc))
			{
				fn(*node, *mesh, node->WorldMatrix);
			}
		}

		const auto& children = node->GetChildren();
		for (auto& child : children)
		{
			ForEachMeshNodeRecursive(child.get(), fn);
		}
	}

	void Scene::ForEachCameraNodeRecursive(const SceneNode* node, const CameraVisitor& fn) const
	{
		if (auto* cc = node->GetComponent<CameraComponent>())
		{
			fn(*node, *cc, node->WorldMatrix);
		}

		const auto& children = node->GetChildren();
		for (auto& child : children)
		{
			ForEachCameraNodeRecursive(child.get(), fn);
		}
	}

	void Scene::ForEachLightNodeRecursive(const SceneNode* node, const LightVisitor& fn) const
	{
		if (auto* lc = node->GetComponent<LightComponent>())
		{
			fn(*node, *lc, node->WorldMatrix);
		}

		const auto& children = node->GetChildren();
		for (auto& child : children)
		{
			ForEachLightNodeRecursive(child.get(), fn);
		}
	}
}
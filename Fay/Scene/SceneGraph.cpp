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
		Meshes.clear();
		Materials.clear();
	}
	
	void Scene::TraverseRecursive(SceneNode* node, const SM::Matrix& parentWorld)
	{
		node->WorldMatrix = node->LocalTransform.ToLocalMatrix() * parentWorld;

		const auto& children = node->GetChildren();
		for (auto& child : children)
		{
			TraverseRecursive(child.get(), node->WorldMatrix);
		}
	}
	
	void Scene::ForEachMeshNodeRecursive(const SceneNode* node, const MeshVisitor& fn) const
	{
		if (auto* mc = node->GetComponent<MeshComponent>())
		{
			if (mc->MeshIndex < Meshes.size())
			{
				fn(*node, *Meshes[mc->MeshIndex], node->WorldMatrix);
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
#include "Scene/SceneGraph.h"
#include "Common/Log.h"

namespace fay
{
	SceneNode* SceneNode::AddChild(std::unique_ptr<SceneNode> child)
	{
		child->m_parent = this;

		if (!child->HasComponent<Transform>())
		{
			std::ignore = child->AddComponent<Transform>();
		}

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
		{
			return m_meshCollections[index].get();
		}
		return nullptr;
	}

	const Mesh* Scene::ResolveMesh(const SceneMeshComponent& comp) const
	{
		if (comp.CollectionIndex < m_meshCollections.size())
		{
			auto& collection = m_meshCollections[comp.CollectionIndex];
			if (comp.MeshIndex < collection->Meshes.size())
			{
				return collection->Meshes[comp.MeshIndex].get();
			}
		}
		return nullptr;
	}

	const std::vector<std::unique_ptr<Material>>* Scene::ResolveMaterials(const SceneMeshComponent& comp) const
	{
		if (comp.CollectionIndex < m_meshCollections.size())
		{
			return &m_meshCollections[comp.CollectionIndex]->Materials;
		}
		return nullptr;
	}

	void Scene::PrintSceneTree()
	{
		std::stringstream ss;

		ss << "Scene Tree:" << std::endl;

		PrintSceneTreeInternal(ss, m_root.get());

		ss << std::endl;
		Log::Info("{}", ss.str());
	}

	void Scene::PrintSceneTreeInternal(std::stringstream& out, const SceneNode* node)
	{
		auto attachComponentInfo = [](std::stringstream& out, const SceneNode* node)
		{
			if (!node->HasAnyComponent())
			{
				return;
			}

			out << "[";

			if (node->HasComponent<SceneMeshComponent>())
			{
				out<< "Mesh";
			}

			//TODO: add other components here
			out << "]";
		};

		out << node->GetName() << " ";
		attachComponentInfo(out, node);
		out << std::endl;

		if (node->HasChildren())
		{
			auto& children = node->GetChildren();
			for (auto& child : children)
			{
				out << "   ";
				PrintSceneTreeInternal(out, child.get());
			}			
		}
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
}
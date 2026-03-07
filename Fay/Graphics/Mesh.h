#pragma once
#include "Graphics/GraphicsTypes.h"

namespace fay
{
    struct SubMesh
    {
        u32 IndexOffset   = 0;
        u32 IndexCount    = 0;
        u32 VertexOffset  = 0;
        i32 MaterialIndex = -1;   // index into Scene::Materials, -1 = default
        BoundingBox Bounds;
    };

    class Mesh
    {
        friend class GLTFImporter;

    public:
        Mesh() = default;
        ~Mesh() = default;

        Mesh(const Mesh&) = delete;
        Mesh& operator=(const Mesh&) = delete;
        Mesh(Mesh&&) noexcept = default;
        Mesh& operator=(Mesh&&) noexcept = default;

        [[nodiscard]] inline const std::string_view GetName() const { return m_name; }
        [[nodiscard]] inline const std::vector<SubMesh>& GetSubMeshes() const { return m_subMeshes; }
        [[nodiscard]] inline nvrhi::IBuffer* GetVertexBuffer() const { return m_vertexBuffer; }
        [[nodiscard]] inline nvrhi::IBuffer* GetIndexBuffer() const { return m_indexBuffer; }
        [[nodiscard]] inline const BoundingBox& GetBounds() const { return m_bounds; }
        [[nodiscard]] inline u32 GetVertexCount() const { return m_vertexCount; }
        [[nodiscard]] inline u32 GetIndexCount() const { return m_indexCount; }

    private:
        std::string          m_name;
        std::vector<SubMesh> m_subMeshes;
        nvrhi::BufferHandle  m_vertexBuffer;
        nvrhi::BufferHandle  m_indexBuffer;
        BoundingBox          m_bounds;
        u32                  m_vertexCount = 0;
        u32                  m_indexCount = 0;
    };
}
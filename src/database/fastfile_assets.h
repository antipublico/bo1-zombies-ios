#pragma once

#include "fastfile_inspection.h"
#include "db_rawfile_types.h"
#include "db_techset_types.h"
#include "db_xmodel_header.h"
#include "db_xsurface_header.h"
#include <stringed/localize_entry.h>
#include <universal/com_stringtable_types.h>
#include <memory>
#include <optional>

namespace bo1::fastfile
{
namespace detail { struct AssetStorage; class AssetBuilder; }

enum class AssetStop { Complete, UnsupportedType, UnsupportedRootReference, UnsupportedModelBody };

// Read-only CPU preview. Bone IDs are still serialized script-string indices,
// not resolved engine strings. Arrays remain separate from any native XModel.
struct XSurfaceCollisionPreview
{
    uint32_t nodeCount = 0;
    uint32_t leafCount = 0;
    std::vector<uint8_t> nodes;
    std::vector<uint8_t> leaves;
};

struct XSurfaceBodyPreview
{
    std::vector<uint8_t> blendIndices;
    std::vector<uint8_t> tensionData;
    std::vector<uint8_t> packedVertices;
    std::vector<uint8_t> rigidLists;
    std::vector<uint8_t> triangleIndices;
    std::vector<XSurfaceCollisionPreview> inlineCollisionTrees;
    uint32_t unresolvedFields = 0;
    uint64_t inlineBodyBytes = 0;
};

struct XModelSkeletonPreview
{
    std::string name;
    std::vector<uint8_t> boneNameBytes;
    std::vector<uint8_t> parentList;
    std::vector<uint8_t> quaternions;
    std::vector<uint8_t> translations;
    std::vector<uint8_t> classifications;
    std::vector<uint8_t> baseMatrices;
    uint64_t inlineArrayBytes = 0;
    // Bit 0..5 denotes a referenced (non-inline) child array in the order above.
    uint32_t unresolvedFields = 0;
    // Complete XSurface headers are read as a contiguous array in block 4.
    // Their nested vertex/collision/material resources are not materialized.
    std::vector<XSurfaceHeader> surfaceHeaders;
    // CPU-owned data only, never raw Direct3D GPU handles.
    std::vector<XSurfaceBodyPreview> surfaceBodies;
    uint64_t surfaceHeaderBytes = 0;
    uint64_t decodedGeometryBytes = 0;
    uint64_t declaredVertices = 0;
    uint64_t declaredTriangles = 0;
};

struct NativeAsset
{
    uint32_t type = 0;
    const RawFile* rawfile = nullptr;
    const StringTable* stringTable = nullptr;
    const LocalizeEntry* localize = nullptr;
    const MaterialTechniqueSet* techniqueSet = nullptr;
    bool alias = false;
};

struct AssetLoadLimits
{
    InspectionLimits inspection{};
    // Bounds accounted payload and bookkeeping sizes, excluding allocator
    // overhead and container spare capacity. Inspection has its own limit.
    size_t maxNativeBytes = 32 * 1024 * 1024;
};

struct AssetPrefix
{
    Inspection inspection;
    std::vector<NativeAsset> entries;
    AssetStop stop = AssetStop::Complete;
    size_t nextAsset = 0;
    // Preview of the first XModel whose nested geometry has not been loaded.
    // This is NOT a reconstructed or registered native model.
    std::optional<XModelHeader> pendingXModel;
    std::optional<XModelSkeletonPreview> pendingXModelSkeleton;
    uint64_t decodedAssetBytes = 0;
    // Budget accounting (including temporary data), not process RSS.
    uint64_t nativeBytes = 0;

private:
    // Pointers in entries stay valid across copies/moves of the owning result.
    std::shared_ptr<detail::AssetStorage> storage;
    friend class detail::AssetBuilder;
};

// Access the engine's variable-length trailing pass storage without indexing
// beyond the declared passArray[1]. The technique must belong to a live prefix.
const MaterialPass* TechniquePassAt(const MaterialTechnique& technique, size_t index);

// Materialize consecutive RawFile, StringTable, LocalizeEntry and TechniqueSet roots.
// in directory order. Stop before the first unknown body; never scan or skip it.
// Always verify the complete compressed stream before publishing any pointers.
// No global engine registry is modified and scripts are not executed.
// Shader programs remain original SM3 tokens; GPU handles stay null/unloaded.
InspectionResult LoadAssetPrefix(const char* path, AssetPrefix& output, std::string& error,
    const AssetLoadLimits& limits = {});
}

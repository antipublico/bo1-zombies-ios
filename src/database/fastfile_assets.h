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

// Diagnostic-only snapshot of the first inline Material header nested in an
// XModel. Child pointers/tables remain serialized tags, never native pointers.
struct XMaterialTexturePreview
{
    uint32_t nameHash = 0;
    uint32_t imageOrWaterTag = 0;
    uint8_t nameStart = 0;
    uint8_t nameEnd = 0;
    uint8_t samplerState = 0;
    uint8_t semantic = 0;
    uint8_t mature = 0;
};

// Packed, 52-byte Win32 GfxImage header. A preview only: it is not a decoded
// texture, and no GfxImageLoadDef pixel bytes are read or uploaded to a GPU.
struct XImageHeaderPreview
{
    size_t textureSlot = 0;
    std::string name;
    uint32_t textureTag = 0;
    uint32_t nameTag = 0;
    uint32_t baseSize = 0;
    uint32_t loadedSize = 0;
    uint32_t pixelsTag = 0;
    uint32_t hash = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    uint16_t depth = 0;
    uint8_t mapType = 0;
    uint8_t semantic = 0;
    uint8_t category = 0;
    uint8_t levelCount = 0;
    uint8_t streaming = 0;
    bool delayLoadPixels = false;
    // GfxImageLoadDef starts with a 12-byte header. Only inspect that header
    // for inline -1 and -2 load-def pointers; the resourceSize pixel bytes
    // remain in the file and are not copied, decoded or uploaded.
    bool inlineLoadDefHeader = false;
    // GfxTextureLoad tag -2 calls DB_InsertPointer, reserving an aligned
    // 4-byte alias slot in block 4 before reading the 12-byte TEMP header.
    bool loadDefInsertedAlias = false;
    bool loadDefPending = false;
    uint8_t loadDefLevels = 0;
    uint8_t loadDefFlags = 0;
    int32_t loadDefFormat = 0;
    int32_t loadDefResourceSize = 0;
};

struct XMaterialHeaderPreview
{
    size_t slot = 0;
    std::string name;
    uint32_t nameTag = 0;
    uint32_t gameFlags = 0;
    uint32_t techniqueSetTag = 0;
    uint32_t textureTableTag = 0;
    uint32_t constantTableTag = 0;
    uint32_t stateBitsTableTag = 0;
    uint8_t sortKey = 0;
    uint8_t textureAtlasRows = 0;
    uint8_t textureAtlasColumns = 0;
    uint8_t textureCount = 0;
    uint8_t constantCount = 0;
    uint8_t stateBitsCount = 0;
    uint8_t stateFlags = 0;
    uint8_t cameraRegion = 0;
    uint8_t maxStreamedMips = 0;
    // Only the fixed, consecutive TextureDefArray records are decoded.
    // Their image/water children may follow in FILE and remain unresolved.
    std::vector<XMaterialTexturePreview> textureHeaders;
    uint64_t textureHeaderBytes = 0;
    bool nestedTechniqueSetPending = false;
    bool textureChildrenPending = false;
    bool textureTableReferenced = false;
    // First nested image (if it comes before water or other recursive data).
    std::optional<XImageHeaderPreview> firstInlineImage;
    bool inlineWaterBeforeImage = false;
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
    // Load_MaterialHandleArray reads its Win32 pointer-slot table before
    // any nested Material payloads. These are raw tags, not native pointers.
    std::vector<uint32_t> materialHandleTags;
    uint64_t materialHandleBytes = 0;
    uint32_t inlineMaterials = 0;
    uint32_t referencedMaterials = 0;
    uint32_t emptyMaterialSlots = 0;
    // Only populated when explicitly requested by the CLI. Recursive material
    // children are still unresolved; only the first inline header is read.
    std::optional<XMaterialHeaderPreview> firstInlineMaterial;
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
    // Opt-in diagnostic: decode the first nested, 192-byte Win32 Material
    // header and name after its parent XModel's material pointer table.
    // Leave false for strict header-only callers and existing fixtures.
    bool previewFirstMaterialHeader = false;
    // Enables the first 52-byte GfxImage header and its XString name.
    // Requires previewFirstMaterialHeader; no GfxImageLoadDef/pixels are read.
    bool previewFirstImageHeader = false;
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

#pragma once

#include "fastfile_inspection.h"
#include "db_rawfile_types.h"
#include "db_techset_types.h"
#include <stringed/localize_entry.h>
#include <universal/com_stringtable_types.h>
#include <memory>

namespace bo1::fastfile
{
namespace detail { struct AssetStorage; class AssetBuilder; }

enum class AssetStop { Complete, UnsupportedType, UnsupportedRootReference };

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

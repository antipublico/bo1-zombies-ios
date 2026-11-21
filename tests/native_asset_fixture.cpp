#include "native_asset_fixture.h"
#include <database/fastfile_assets.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <utility>

namespace bo1::tests
{
namespace
{
using Bytes = std::vector<unsigned char>;
constexpr std::uint32_t Inline = 0xFFFFFFFFu;
constexpr std::uint32_t Insert = 0xFFFFFFFEu;

constexpr std::uint32_t Block4(std::uint32_t offset)
{
    return 0x80000000u + offset + 1;
}

void U16(Bytes& bytes, std::uint16_t value)
{
    bytes.push_back(static_cast<unsigned char>(value));
    bytes.push_back(static_cast<unsigned char>(value >> 8));
}

void U32(Bytes& bytes, std::uint32_t value)
{
    for (unsigned int shift = 0; shift < 32; shift += 8)
        bytes.push_back(static_cast<unsigned char>(value >> shift));
}

void Text(Bytes& bytes, const char* value)
{
    bytes.insert(bytes.end(), value, value + std::strlen(value) + 1);
}

std::uint32_t Adler32(const unsigned char* bytes, size_t size)
{
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (size_t i = 0; i < size; ++i)
    {
        a = (a + bytes[i]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

bool Matches(const char* actual, const char* expected)
{
    return actual && std::strcmp(actual, expected) == 0;
}

bool Verify(const fastfile::AssetPrefix& loaded, std::string& error, bool insertedAlias = false)
{
    const auto check = [&error](bool condition, const char* reason)
    {
        if (!condition)
            error = std::string("Native asset fixture: ") + reason;
        return condition;
    };
    if (!check(loaded.stop == fastfile::AssetStop::Complete, "loader did not reach the end")
        || !check(loaded.entries.size() == 4 && loaded.nextAsset == 4, "wrong materialized count or next cursor")
        || !check(loaded.inspection.assets.size() == 4 && loaded.inspection.list.assetCount == 4,
            "wrong directory count")
        || !check(loaded.inspection.scriptStrings.empty() && loaded.inspection.list.scriptStringCount == 0,
            "unexpected script strings")
        || !check(loaded.inspection.metadataBytes == 84, "metadata byte cursor is not 84")
        || !check(loaded.decodedAssetBytes == 137, "asset byte cursor includes block alignment or is truncated")
        || !check(loaded.inspection.expandedBytes == 221 && loaded.inspection.file.size == 185,
            "wrong complete payload size")
        || !check(loaded.inspection.metadataBytes + loaded.decodedAssetBytes == loaded.inspection.expandedBytes,
            "decoded prefix does not cover the complete payload"))
        return false;

    const auto& rawEntry = loaded.entries[0];
    const auto& tableEntry = loaded.entries[1];
    const auto& localizedEntry = loaded.entries[2];
    const auto& aliasEntry = loaded.entries[3];
    if (!check(rawEntry.type == 36 && rawEntry.rawfile && !rawEntry.alias
            && !rawEntry.stringTable && !rawEntry.localize, "wrong RawFile variant")
        || !check(tableEntry.type == 37 && tableEntry.stringTable && !tableEntry.alias
            && !tableEntry.rawfile && !tableEntry.localize, "wrong StringTable variant")
        || !check(localizedEntry.type == 23 && localizedEntry.localize && !localizedEntry.alias
            && !localizedEntry.rawfile && !localizedEntry.stringTable, "wrong LocalizeEntry variant")
        || !check(aliasEntry.alias && !aliasEntry.localize && (insertedAlias
            ? aliasEntry.type == 37 && aliasEntry.stringTable == tableEntry.stringTable && !aliasEntry.rawfile
            : aliasEntry.type == 36 && aliasEntry.rawfile == rawEntry.rawfile && !aliasEntry.stringTable),
            "root alias did not preserve pointer identity"))
        return false;

    const auto* raw = rawEntry.rawfile;
    const auto* table = tableEntry.stringTable;
    const auto* localize = localizedEntry.localize;
    if (!check(Matches(raw->name, "script/test.gsc") && raw->len == 7 && raw->buffer
            && std::memcmp(raw->buffer, "fixture", 8) == 0, "RawFile name, length or terminated buffer differs")
        || !check(Matches(table->name, "table.csv") && table->columnCount == 2 && table->rowCount == 2
            && table->values && table->cellIndex, "StringTable layout or dimensions differ")
        || !check(Matches(table->values[0].string, "alpha") && table->values[0].hash == 0x11223344,
            "first table cell differs")
        || !check(table->values[1].string == raw->name && table->values[1].hash == -7,
            "XString reference or signed hash differs")
        || !check(Matches(table->values[2].string, "gamma") && table->values[2].hash == 0x7FFFFFFF,
            "third table cell differs")
        || !check(Matches(table->values[3].string, "") && table->values[3].hash == 0,
            "empty table cell differs")
        || !check(table->cellIndex[0] == 1 && table->cellIndex[1] == 3
            && table->cellIndex[2] == 0 && table->cellIndex[3] == 2,
            "four signed 16-bit indices differ")
        || !check(localize->value == table->values[0].string && Matches(localize->name, "KEY_ALPHA"),
            "localized string reference or name differs"))
        return false;
    return true;
}
}

std::vector<unsigned char> NativeAssetFixture()
{
    Bytes body;
    U32(body, 0); U32(body, 0); // No script-string table.
    U32(body, 4); U32(body, Inline);
    U32(body, 36); U32(body, Inline);
    U32(body, 37); U32(body, Insert);
    U32(body, 23); U32(body, Inline);
    U32(body, 36); U32(body, Block4(4)); // Alias to root zero's directory pointer slot.

    // RawFile in TEMP block 0. Buffer is a presence tag, not an XString tag.
    U32(body, Inline); U32(body, 7); U32(body, 1);
    Text(body, "script/test.gsc"); // B4 offset 32, 16 bytes.
    Text(body, "fixture");          // B4 offset 48, 8 bytes.

    // The -2 root reserves an inserted pointer at B4 offset 56 without reading
    // any FILE bytes. Its children therefore start at B4 offset 60.
    U32(body, Inline); U32(body, 2); U32(body, 2); U32(body, 1); U32(body, 1);
    Text(body, "table.csv"); // B4 offset 60, 10 bytes; values align to offset 72.
    U32(body, Inline);     U32(body, 0x11223344);
    U32(body, Block4(32)); U32(body, 0xFFFFFFF9); // Previously decoded raw name, hash -7.
    U32(body, Inline);     U32(body, 0x7FFFFFFF);
    U32(body, Inline);     U32(body, 0);
    Text(body, "alpha"); // B4 offset 104.
    Text(body, "gamma"); // B4 offset 110.
    Text(body, "");      // B4 offset 116; indices align to 118.
    U16(body, 1); U16(body, 3); U16(body, 0); U16(body, 2);

    U32(body, Block4(104)); U32(body, Inline); // LocalizeEntry value/name tags.
    Text(body, "KEY_ALPHA"); // B4 offset 126, 10 bytes.

    Bytes payload;
    U32(payload, static_cast<std::uint32_t>(body.size()));
    U32(payload, 0);
    U32(payload, 64); // TEMP resets for each root; largest root is 20 bytes.
    U32(payload, 0); U32(payload, 0); U32(payload, 0);
    U32(payload, 4096); // Block 4 contains directory and native asset children.
    U32(payload, 0); U32(payload, 0);
    payload.insert(payload.end(), body.begin(), body.end());

    Bytes file = {'I', 'W', 'f', 'f', 'u', '1', '0', '0'};
    U32(file, 473);
    file.push_back(0x78); file.push_back(0x01); // zlib header, no dictionary.
    file.push_back(0x01); // Final stored DEFLATE block, aligned to next byte.
    const auto length = static_cast<std::uint16_t>(payload.size());
    U16(file, length); U16(file, static_cast<std::uint16_t>(~length));
    file.insert(file.end(), payload.begin(), payload.end());
    const auto checksum = Adler32(payload.data(), payload.size());
    for (int shift = 24; shift >= 0; shift -= 8)
        file.push_back(static_cast<unsigned char>(checksum >> shift));
    return file;
}

bool CheckNativeAssetFixture(const char* temporaryPath, std::string& error)
{
    error.clear();
    if (!temporaryPath)
    {
        error = "Native asset fixture: missing temporary path";
        return false;
    }
    struct RemoveFile
    {
        const char* path;
        ~RemoveFile() { std::remove(path); }
    } cleanup{temporaryPath};
    auto bytes = NativeAssetFixture();
    const auto writeFile = [&]()
    {
        std::unique_ptr<FILE, decltype(&std::fclose)> file(std::fopen(temporaryPath, "wb"), &std::fclose);
        if (!file || std::fwrite(bytes.data(), 1, bytes.size(), file.get()) != bytes.size()
            || std::fflush(file.get()) != 0)
        {
            error = "Native asset fixture: could not write temporary file";
            return false;
        }
        return true;
    };
    if (!writeFile()) return false;

    fastfile::AssetPrefix original;
    if (fastfile::LoadAssetPrefix(temporaryPath, original, error) != fastfile::InspectionResult::Ok
        || !Verify(original, error))
        return false;

    const auto* raw = original.entries[0].rawfile;
    const auto* table = original.entries[1].stringTable;
    fastfile::AssetPrefix copied(original);
    original = {};
    if (!Verify(copied, error) || copied.entries[0].rawfile != raw || copied.entries[1].stringTable != table)
    {
        if (error.empty()) error = "Native asset fixture: copy did not retain storage identity";
        return false;
    }

    fastfile::AssetPrefix moved(std::move(copied));
    copied = {};
    if (!Verify(moved, error))
        return false;
    fastfile::AssetPrefix assigned;
    assigned = moved;
    moved.entries[0].rawfile = nullptr;
    if (!Verify(assigned, error))
        return false;
    moved = {};
    copied = std::move(assigned);
    assigned = {};
    if (!Verify(copied, error)) return false;

    // Change only the fourth directory root to alias the -2 insertion slot.
    // The inserted pointer reserves block bytes but has no corresponding FILE
    // bytes, which makes confusing those cursors observable in this variant.
    const size_t lastRoot = 19 + 36 + 16 + 3 * 8;
    bytes[lastRoot] = 37;
    const auto inserted = Block4(56);
    for (unsigned int i = 0; i < 4; ++i)
        bytes[lastRoot + 4 + i] = static_cast<unsigned char>(inserted >> (i * 8));
    const auto checksum = Adler32(bytes.data() + 19, bytes.size() - 19 - 4);
    for (unsigned int i = 0; i < 4; ++i)
        bytes[bytes.size() - 4 + i] = static_cast<unsigned char>(checksum >> (24 - i * 8));
    if (!writeFile()) return false;
    fastfile::AssetPrefix insertedResult;
    if (fastfile::LoadAssetPrefix(temporaryPath, insertedResult, error) != fastfile::InspectionResult::Ok
        || !Verify(insertedResult, error, true))
        return false;
    // Reloading the same path must not invalidate a previous result's strings.
    return Verify(copied, error);
}
}

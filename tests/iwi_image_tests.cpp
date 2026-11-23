#include <platform/bo1_iwi_image.h>
#include <platform/bo1_bytecode_io.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
unsigned checks = 0, failures = 0;
void Check(bool c, const char* name)
{
    ++checks;
    if (!c) { ++failures; std::fprintf(stderr, "FAIL: %s\n", name); }
}
std::vector<unsigned char> Fixture(std::uint16_t width, std::uint16_t height,
    std::size_t extraSmallMips, std::size_t topMipBytes)
{
    std::vector<unsigned char> f(48 + extraSmallMips + topMipBytes, 0);
    f[0] = 'I'; f[1] = 'W'; f[2] = 'i'; f[3] = 13;
    f[4] = 12; f[5] = 0x10;
    bo1::bytecode::Store<std::int16_t>(f.data() + 6, width);
    bo1::bytecode::Store<std::int16_t>(f.data() + 8, height);
    bo1::bytecode::Store<std::int16_t>(f.data() + 10, 1);
    bo1::bytecode::Store<float>(f.data() + 12, 1.0f);
    bo1::bytecode::Store<std::int32_t>(f.data() + 16, static_cast<std::int32_t>(f.size()));
    bo1::bytecode::Store<std::int32_t>(f.data() + 20,
        static_cast<std::int32_t>(48 + extraSmallMips));
    const auto start = f.size() - topMipBytes;
    for (std::size_t i = 0; i < topMipBytes; i += 16)
    {
        for (std::size_t j = 0; j < 8; ++j) f[start + i + j] = 0xff;
        bo1::bytecode::Store<std::uint16_t>(f.data() + start + i + 8, 0xf800);
        bo1::bytecode::Store<std::uint16_t>(f.data() + start + i + 10, 0x07e0);
    }
    return f;
}
}

int main()
{
    using namespace bo1::texture;
    auto f = Fixture(4, 4, 0, 16);
    IwiHeader header{};
    Check(ParseIwiHeader(f.data(), 48, f.size(), header) == IwiParseResult::Ok,
        "version 13 IWi 48-byte header");
    Check(header.version == 13 && header.format == 12 && header.width == 4
        && header.height == 4 && header.depth == 1 && header.flags == 0x10
        && header.gamma == 1.0f && header.fileSizeForPicmip[0] == 64,
        "portable IWI header and 8 picmip file lengths");
    Check(std::strcmp(IwiFormatName(header.format), "DXT3 / BC2") == 0
        && std::strcmp(IwiFormatName(11), "DXT1 / BC1") == 0
        && std::strcmp(IwiFormatName(13), "DXT5 / BC3") == 0,
        "IWI format 12 is DXT3 but Direct3D FourCC uses 861165636");
    std::uint64_t offset = 0;
    std::size_t byteCount = 0;
    Check(IwiBC2TopMipRange(header, offset, byteCount)
        && offset == 48 && byteCount == 16, "one 4x4 IWI BC2 mip at byte 48");
    std::vector<std::uint8_t> pixels;
    Check(DecodeBC2RGBA(f.data() + offset, byteCount, header.width, header.height, pixels)
        && pixels.size() == 64 && pixels[0] == 255 && pixels[1] == 0
        && pixels[2] == 0 && pixels[3] == 255,
        "extract top IWI DXT3 mip and decompress to RGBA8");

    f = Fixture(8, 8, 16, 64);
    f[48] = 0; // Smaller mip must not be mistaken for the top level.
    Check(ParseIwiHeader(f.data(), 48, f.size(), header) == IwiParseResult::Ok
        && IwiBC2TopMipRange(header, offset, byteCount)
        && offset == 64 && byteCount == 64,
        "IWI stores smaller mipmaps first; biggest mip blocks last");
    Check(DecodeBC2RGBA(f.data() + offset, byteCount, header.width, header.height, pixels)
        && pixels.size() == 256 && pixels[0] == 255,
        "decode 8x8 IWI BC2 last four blocks");
    auto bad = f;
    bad[0] = 'X';
    Check(ParseIwiHeader(bad.data(), 48, bad.size(), header) == IwiParseResult::NotIwi
        && header.width == 0, "wrong magic is rejected and output cleared");
    bad = f; bad[3] = 14;
    Check(ParseIwiHeader(bad.data(), 48, bad.size(), header)
        == IwiParseResult::UnsupportedVersion, "reject wrong IWI version");
    bad = f;
    bo1::bytecode::Store<std::int16_t>(bad.data() + 6, -1);
    Check(ParseIwiHeader(bad.data(), 48, bad.size(), header)
        == IwiParseResult::InvalidDimensions, "reject negative IWI dimension");
    bad = f;
    bo1::bytecode::Store<std::int32_t>(bad.data() + 16, 48);
    Check(ParseIwiHeader(bad.data(), 48, bad.size(), header)
        == IwiParseResult::InvalidFileSize, "enforce original engine full length");
    Check(ParseIwiHeader(f.data(), 47, f.size(), header)
        == IwiParseResult::Truncated, "reject truncated IWI header");
    Check(ParseIwiHeader(nullptr, 48, f.size(), header)
        == IwiParseResult::Truncated, "reject missing IWI bytes");

    bad = f;
    bad[4] = 11; // DXT1 is recognized but not decoded by the BC2 path.
    Check(ParseIwiHeader(bad.data(), 48, bad.size(), header) == IwiParseResult::Ok
        && !IwiBC2TopMipRange(header, offset, byteCount),
        "do not decode DXT1 using BC2");
    bad = f;
    bad[5] = 0x14; // cube/volume metadata.
    Check(ParseIwiHeader(bad.data(), 48, bad.size(), header) == IwiParseResult::Ok
        && !IwiBC2TopMipRange(header, offset, byteCount),
        "do not read cubemap or volume data as a 2D BC2 texture");
    bad = f;
    bo1::bytecode::Store<std::int32_t>(bad.data() + 20, 48);
    Check(ParseIwiHeader(bad.data(), 48, bad.size(), header) == IwiParseResult::Ok
        && !IwiBC2TopMipRange(header, offset, byteCount),
        "picmip boundaries must agree with the top-level block size");

    std::printf("%u IWI v13 texture checks, %u failures\n", checks, failures);
    return failures != 0;
}

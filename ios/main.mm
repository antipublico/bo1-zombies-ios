#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <SceneKit/SceneKit.h>

#include <database/db_fastfile_format.h>
#include <database/fastfile_inspection.h>
#include <database/fastfile_assets.h>
#include <database/db_xmodel_header.h>
#include <database/db_xsurface_header.h>
#include <platform/bo1_texture_bc.h>
#include <platform/bo1_iwi_image.h>
#include <platform/bo1_model_mesh.h>
#include <platform/bo1_bytecode_io.h>
#include <clientscript/cscr_bytecode.h>
#include <steam/steam_api.h>
#include <universal/narrow32.h>
#include "../tests/native_asset_fixture.h"
#include "../tests/techset_fixture.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

static bool runSelfTest = false;
static bool showFixturePreview = false;
static bool showIwiFixturePreview = false;
static bool showModelFixturePreview = false;

// Four large, uniform BC2 quadrants make screenshot sampling independent of
// scaling/filtering at block boundaries. No original game pixels are used.
static NSData* IwiPreviewFixture()
{
    constexpr size_t width = 128, height = 96;
    std::vector<unsigned char> bytes(48 + width * height, 0);
    bytes[0] = 'I'; bytes[1] = 'W'; bytes[2] = 'i';
    bytes[3] = 13; bytes[4] = 12; bytes[5] = 0x10;
    bo1::bytecode::Store<uint16_t>(bytes.data() + 6, width);
    bo1::bytecode::Store<uint16_t>(bytes.data() + 8, height);
    bo1::bytecode::Store<uint16_t>(bytes.data() + 10, 1);
    bo1::bytecode::Store<uint32_t>(bytes.data() + 16, bytes.size());
    bo1::bytecode::Store<uint32_t>(bytes.data() + 20, 48);
    const uint16_t colors[] = {0xf800, 0x07e0, 0x001f, 0xffe0};
    const unsigned char alpha[] = {0xff, 0x88, 0xff, 0x00};
    for (size_t y = 0; y < height / 4; ++y)
        for (size_t x = 0; x < width / 4; ++x)
        {
            const size_t quadrant = (y >= height / 8 ? 2 : 0) + (x >= width / 8 ? 1 : 0);
            auto* block = bytes.data() + 48 + (y * (width / 4) + x) * 16;
            std::memset(block, alpha[quadrant], 8);
            bo1::bytecode::Store<uint16_t>(block + 8, colors[quadrant]);
            bo1::bytecode::Store<uint16_t>(block + 10, colors[quadrant]);
        }
    return [NSData dataWithBytes:bytes.data() length:bytes.size()];
}

// The runner separately samples the simctl screenshot at these coordinates.
// This marker checks decoded pixels and layout; it alone is not a UI pass.
static NSDictionary* IwiFixtureLayoutReport(UIImageView* view)
{
    auto fail = [](NSString* reason) -> NSDictionary* {
        return @{@"status": @"fail", @"error": reason};
    };
    UIWindow* window = view.window;
    if (!window || window.hidden || view.hidden || view.alpha < 1 || !view.image.CGImage)
        return fail(@"Texture view is hidden, detached or has no image");
    for (UIView* ancestor = view.superview; ancestor; ancestor = ancestor.superview)
        if (ancestor.hidden || ancestor.alpha < 1)
            return fail(@"An ancestor hides the texture view");
    CGImageRef image = view.image.CGImage;
    if (CGImageGetWidth(image) != 128 || CGImageGetHeight(image) != 96
        || CGImageGetBitsPerPixel(image) != 32 || CGImageGetBytesPerRow(image) != 128 * 4)
        return fail(@"Unexpected synthetic image dimensions or pixel layout");
    const uint8_t decoded[4][4] = {{255,0,0,255}, {0,136,0,136}, {0,0,255,255}, {0,0,0,0}};
    CFDataRef data = CGDataProviderCopyData(CGImageGetDataProvider(image));
    bool valid = data && CFDataGetLength(data) == 128 * 96 * 4;
    if (valid)
        for (size_t i = 0; i < 4; ++i)
        {
            const size_t x = i % 2 ? 96 : 32, y = i / 2 ? 72 : 24;
            valid = valid && std::memcmp(CFDataGetBytePtr(data) + (y * 128 + x) * 4, decoded[i], 4) == 0;
        }
    if (data) CFRelease(data);
    if (!valid) return fail(@"Synthetic colors or premultiplied alpha differ");

    const CGFloat scale = MIN(view.bounds.size.width / 128, view.bounds.size.height / 96);
    const CGSize size = CGSizeMake(128 * scale, 96 * scale);
    const CGRect rect = CGRectMake((view.bounds.size.width - size.width) / 2,
        (view.bounds.size.height - size.height) / 2, size.width, size.height);
    const CGRect screenRect = [view convertRect:rect toView:window];
    if (size.width < 64 || size.height < 48 || !CGRectContainsRect(window.bounds, screenRect))
        return fail(@"Texture is too small or outside the visible window");
    const uint8_t composited[4][4] = {{255,0,0,255}, {119,255,119,255}, {0,0,255,255}, {255,255,255,255}};
    NSMutableArray* samples = [NSMutableArray array];
    for (size_t i = 0; i < 4; ++i)
    {
        const CGPoint local = CGPointMake(rect.origin.x + size.width * (i % 2 ? .75 : .25),
            rect.origin.y + size.height * (i / 2 ? .75 : .25));
        const CGPoint point = [view convertPoint:local toView:window];
        [samples addObject:@{@"u": @(point.x / window.bounds.size.width),
            @"v": @(point.y / window.bounds.size.height),
            @"rgba": @[@(composited[i][0]), @(composited[i][1]), @(composited[i][2]), @(composited[i][3])]}];
    }
    return @{@"status": @"pass", @"image_width": @128, @"image_height": @96,
        @"window_width": @(window.bounds.size.width), @"window_height": @(window.bounds.size.height),
        @"samples": samples};
}

// Read only the 48-byte IWI metadata and its largest BC2 mip from the
// security-scoped file. UIKit receives a fully owned, premultiplied RGBA8
// CGImage; no compressed game content is bundled or uploaded.
static UIImage* IwiPreviewForPath(const char* path, NSString** details)
{
    auto fail = [&](NSString* reason) -> UIImage*
    {
        if (details) *details = reason;
        return nil;
    };
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream)
        return fail(@"No se pudo abrir el archivo .iwi.");
    const auto end = stream.tellg();
    if (end < std::streampos(bo1::texture::IwiHeaderBytes))
        return fail(@"El archivo .iwi no contiene una cabecera de 48 bytes.");
    const auto total = static_cast<std::uint64_t>(end);
    stream.seekg(0, std::ios::beg);
    std::array<unsigned char, bo1::texture::IwiHeaderBytes> disk{};
    stream.read(reinterpret_cast<char*>(disk.data()),
        static_cast<std::streamsize>(disk.size()));
    if (!stream)
        return fail(@"No se pudo leer la cabecera .iwi.");

    bo1::texture::IwiHeader header{};
    if (bo1::texture::ParseIwiHeader(disk.data(), disk.size(), total, header)
            != bo1::texture::IwiParseResult::Ok)
        return fail(@"El archivo no es una imagen IWi v13 válida.");
    if (header.format != bo1::texture::IwiFormatDXT3)
        return fail([NSString stringWithFormat:
            @"Formato .iwi %u (%s) detectado. La vista todavía admite DXT3/BC2.",
            unsigned(header.format), bo1::texture::IwiFormatName(header.format)]);

    std::uint64_t offset = 0;
    std::size_t compressedSize = 0;
    if (!bo1::texture::IwiBC2TopMipRange(header, offset, compressedSize))
        return fail(@"No se pudo encontrar el nivel principal DXT3/BC2. "
            @"Las imágenes cúbicas, volumétricas o demasiado grandes aún no se admiten.");
    std::vector<std::uint8_t> compressed(compressedSize);
    stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    stream.read(reinterpret_cast<char*>(compressed.data()),
        static_cast<std::streamsize>(compressed.size()));
    if (!stream)
        return fail(@"La textura DXT3 está incompleta.");
    std::vector<std::uint8_t> rgba;
    if (!bo1::texture::DecodeBC2RGBA(compressed.data(), compressed.size(),
        header.width, header.height, rgba))
        return fail(@"No fue posible descomprimir la imagen BC2.");

    // CoreGraphics bitmap with premultiplied alpha, preserving transparency.
    // The portable decoder returns straight RGBA, so premultiply exactly once.
    for (std::size_t i = 0; i < rgba.size(); i += 4)
    {
        const unsigned alpha = rgba[i + 3];
        for (unsigned channel = 0; channel < 3; ++channel)
            rgba[i + channel] = static_cast<std::uint8_t>(
                (unsigned(rgba[i + channel]) * alpha + 127u) / 255u);
    }
    NSData* bytes = [NSData dataWithBytes:rgba.data() length:rgba.size()];
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGDataProviderRef provider = CGDataProviderCreateWithCFData((__bridge CFDataRef)bytes);
    CGImageRef cgImage = nullptr;
    if (colorSpace && provider)
        cgImage = CGImageCreate(header.width, header.height, 8, 32,
            std::size_t(header.width) * 4, colorSpace,
            static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big),
            provider, nullptr, false, kCGRenderingIntentDefault);
    if (provider) CGDataProviderRelease(provider);
    if (colorSpace) CGColorSpaceRelease(colorSpace);
    if (!cgImage)
        return fail(@"No se pudo crear la imagen de UIKit.");
    UIImage* image = [UIImage imageWithCGImage:cgImage];
    CGImageRelease(cgImage);

    if (details)
        *details = [NSString stringWithFormat:
            @"Textura IWI reconstruida localmente\n\n"
            "Versión: %u\nFormato: DXT3 / BC2\nResolución: %u × %u\n"
            "Nivel principal: %zu bytes BC2\n"
            "Píxeles RGBA8: %zu bytes\n\n"
            "La imagen conserva su transparencia. No se usa Direct3D "
            "ni se inicia una partida.",
            unsigned(header.version), unsigned(header.width),
            unsigned(header.height), compressedSize, rgba.size()];
    return image;
}

// A temporary native SceneKit preview of the *real* XModel LOD0 geometry.
// It uses the iPhone's GPU through SceneKit/Metal, not BO1's D3D9 shaders.
// Materials are not resolved: the user's selected IWI is applied to every
// supported inline surface; the engine's material binding remains future work.
static SCNScene* SceneForMesh(const bo1::model::Mesh& mesh, UIImage* texture)
{
    if (!texture || mesh.vertices.empty() || mesh.indices.empty())
        return nil;
    NSData* packed = [NSData dataWithBytes:mesh.vertices.data()
        length:mesh.vertices.size() * sizeof(bo1::model::Vertex)];
    SCNGeometrySource* positions = [SCNGeometrySource
        geometrySourceWithData:packed
        semantic:SCNGeometrySourceSemanticVertex
        vectorCount:mesh.vertices.size()
        usesFloatComponents:YES componentsPerVector:3
        bytesPerComponent:sizeof(float)
        dataOffset:0 dataStride:sizeof(bo1::model::Vertex)];
    SCNGeometrySource* uv = [SCNGeometrySource
        geometrySourceWithData:packed
        semantic:SCNGeometrySourceSemanticTexcoord
        vectorCount:mesh.vertices.size()
        usesFloatComponents:YES componentsPerVector:2
        bytesPerComponent:sizeof(float)
        dataOffset:offsetof(bo1::model::Vertex, u)
        dataStride:sizeof(bo1::model::Vertex)];
    NSData* faces = [NSData dataWithBytes:mesh.indices.data()
        length:mesh.indices.size() * sizeof(std::uint32_t)];
    SCNGeometryElement* triangles = [SCNGeometryElement
        geometryElementWithData:faces
        primitiveType:SCNGeometryPrimitiveTypeTriangles
        primitiveCount:mesh.indices.size() / 3
        bytesPerIndex:sizeof(std::uint32_t)];
    SCNGeometry* geo = [SCNGeometry geometryWithSources:@[positions, uv]
        elements:@[triangles]];
    SCNMaterial* mat = [SCNMaterial material];
    mat.diffuse.contents = texture;
    mat.lightingModelName = SCNLightingModelConstant;
    mat.doubleSided = YES;
    mat.transparencyMode = SCNTransparencyModeAOne;
    mat.diffuse.wrapS = SCNWrapModeRepeat;
    mat.diffuse.wrapT = SCNWrapModeRepeat;
    geo.materials = @[mat];

    SCNScene* scene = [SCNScene scene];
    SCNNode* node = [SCNNode nodeWithGeometry:geo];
    node.name = [NSString stringWithUTF8String:mesh.name.c_str()] ?: @"XModel LOD0";
    [scene.rootNode addChildNode:node];

    const float x = (mesh.mins[0] + mesh.maxs[0]) * 0.5f;
    const float y = (mesh.mins[1] + mesh.maxs[1]) * 0.5f;
    const float z = (mesh.mins[2] + mesh.maxs[2]) * 0.5f;
    const float dx = mesh.maxs[0] - mesh.mins[0];
    const float dy = mesh.maxs[1] - mesh.mins[1];
    const float dz = mesh.maxs[2] - mesh.mins[2];
    const float radius = std::max(0.2f, std::sqrt(dx * dx + dy * dy + dz * dz) * 0.5f);
    SCNNode* camera = [SCNNode node];
    camera.name = @"BO1 preview camera";
    camera.camera = [SCNCamera camera];
    camera.camera.zNear = 0.01;
    camera.camera.zFar = std::max(100.0, static_cast<double>(radius) * 30.0);
    camera.camera.fieldOfView = 55;
    camera.position = SCNVector3Make(x + radius * 1.3f,
        y + radius * 0.9f, z + radius * 2.2f);
    [camera lookAt:SCNVector3Make(x, y, z)];
    [scene.rootNode addChildNode:camera];
    return scene;
}

static std::shared_ptr<bo1::model::Mesh> SyntheticCrossedPlanes()
{
    // Only a screenshot fixture. This geometry is deliberately invented;
    // actual BO1 geometry is loaded by ExtractLOD0 from a user-supplied .ff.
    auto mesh = std::make_shared<bo1::model::Mesh>();
    mesh->name = "synthetic crossed foliage";
    mesh->vertices = {
        {-1,-1,0,0,1}, {1,-1,0,1,1}, {1,1,0,1,0}, {-1,1,0,0,0},
        {0,-1,-1,0,1}, {0,-1,1,1,1}, {0,1,1,1,0}, {0,1,-1,0,0}
    };
    mesh->indices = {0,1,2,0,2,3,4,5,6,4,6,7};
    mesh->mins[0] = mesh->mins[1] = mesh->mins[2] = -1;
    mesh->maxs[0] = mesh->maxs[1] = mesh->maxs[2] = 1;
    mesh->surfaceCount = 2;
    return mesh;
}

static bool CheckNativeComponents(std::string& error)
{
    static_assert(sizeof(void*) == 8, "The iOS host requires a 64-bit ABI");
    static_assert(sizeof(BO1_NARROW(int)) == 4, "Script slots must remain four bytes");

    int value = 42;
    const char operand[] = {0, 0x78, 0x56, 0x34, 0x12, 0};
    const char* cursor = operand + 1;
    if (Scr_ReadUnsigned(&cursor) != 0x12345678u || cursor != operand + 5)
    {
        error = "Script operand reader did not preserve the four-byte width";
        return false;
    }
    BO1_NARROW(int) slot{};
    slot = &value;
    if (static_cast<int*>(slot) != &value || *static_cast<int*>(slot) != 42)
    {
        error = "Narrow32 pointer round-trip failed";
        return false;
    }
    slot = nullptr;
    if (static_cast<int*>(slot) != nullptr)
    {
        error = "Narrow32 null pointer round-trip failed";
        return false;
    }

    const unsigned char headerBytes[] = {'I', 'W', 'f', 'f', 'u', '1', '0', '0', 0xD9, 1, 0, 0};
    bo1::fastfile::FileHeader header{};
    if (bo1::fastfile::ParseFileHeader(headerBytes, sizeof(headerBytes), header) != bo1::fastfile::ParseResult::Ok ||
        header.version != 473 || header.authenticated)
    {
        error = "Fastfile header decoder failed";
        return false;
    }

    // Validate the actual ARM64 device/simulator ABI against the packed
    // 252-byte Win32 model header. Never cast tags into native pointers.
    unsigned char modelDisk[bo1::fastfile::XModelDiskHeaderSize + 1]{};
    auto* model = modelDisk + 1; // Deliberately unaligned.
    model[4] = 4; // bones
    model[5] = 1; // root bones
    model[6] = 3; // surfaces
    bo1::bytecode::Store<std::uint16_t>(model + 44, 3); // LOD0 surfaces
    bo1::bytecode::Store<std::int16_t>(model + 216, 1); // active LODs
    bo1::bytecode::Store<std::int16_t>(model + 218, -1); // no collision LOD
    bo1::bytecode::Store<std::uint32_t>(model + 32, UINT32_MAX);
    bo1::fastfile::XModelHeader modelHeader{};
    if (!bo1::fastfile::DecodeXModelHeader(model, bo1::fastfile::XModelDiskHeaderSize, modelHeader)
        || modelHeader.numBones != 4 || modelHeader.numSurfaces != 3
        || modelHeader.lods[0].surfaceCount != 3 || modelHeader.surfacesTag != UINT32_MAX
        || modelHeader.collisionLod != -1)
    {
        error = "Packed XModel header decoder failed on ARM64";
        return false;
    }
    model[5] = 5; // Root bone count cannot exceed total bones.
    if (bo1::fastfile::DecodeXModelHeader(model, bo1::fastfile::XModelDiskHeaderSize, modelHeader)
        || modelHeader.numBones != 0)
    {
        error = "Invalid XModel header was accepted or partially published";
        return false;
    }

    // Surface metadata is a separate packed Win32 structure. Exercise the
    // unaligned ARM64 path and the geometry-size calculator without shaders.
    unsigned char surfaceBytes[bo1::fastfile::XSurfaceDiskHeaderSize + 1]{};
    auto* surface = surfaceBytes + 1;
    surface[1] = 1;
    bo1::bytecode::Store<std::uint16_t>(surface + 4, 4);
    bo1::bytecode::Store<std::uint16_t>(surface + 6, 2);
    bo1::bytecode::Store<std::int16_t>(surface + 16, 2);
    bo1::bytecode::Store<std::uint32_t>(surface + 24, UINT32_MAX);
    bo1::bytecode::Store<std::uint32_t>(surface + 32, UINT32_MAX);
    bo1::bytecode::Store<std::uint32_t>(surface + 40, UINT32_MAX);
    bo1::bytecode::Store<std::uint32_t>(surface + 12, UINT32_MAX);
    bo1::fastfile::XSurfaceHeader surfaceHeader{};
    if (!bo1::fastfile::DecodeXSurfaceHeader(
            surface, bo1::fastfile::XSurfaceDiskHeaderSize, surfaceHeader))
    {
        error = "Packed XSurface header decoder failed on ARM64";
        return false;
    }
    const auto geometry = bo1::fastfile::CalculateXSurfaceInlineBytes(surfaceHeader);
    if (geometry.blendIndices != 4 || geometry.packedVertices != 128
        || geometry.rigidLists != 12 || geometry.triangleIndices != 12)
    {
        error = "Packed XSurface inline byte accounting failed";
        return false;
    }
    bo1::bytecode::Store<std::int16_t>(surface + 16, -1);
    if (bo1::fastfile::DecodeXSurfaceHeader(
            surface, bo1::fastfile::XSurfaceDiskHeaderSize, surfaceHeader))
    {
        error = "Invalid XSurface blend counts were accepted";
        return false;
    }

    // Native BC2/Direct3D DXT3 CPU decode. iOS cannot consume the original
    // D3D9 texture object, but can use RGBA8 pixels once the compressed
    // texture bytes are available from the user's own assets.
    unsigned char bc2Block[16]{};
    for (size_t i = 0; i < 8; ++i) bc2Block[i] = 0xff;
    bo1::bytecode::Store<std::uint16_t>(bc2Block + 8, 0xf800);
    bo1::bytecode::Store<std::uint16_t>(bc2Block + 10, 0xf800);
    std::vector<std::uint8_t> bc2Output;
    if (std::strcmp(bo1::texture::FormatName(861165636u), "DXT3 / BC2") != 0
        || !bo1::texture::DecodeBC2RGBA(
            bc2Block, sizeof(bc2Block), 4, 4, bc2Output)
        || bc2Output.size() != 64 || bc2Output[0] != 255
        || bc2Output[1] != 0 || bc2Output[2] != 0
        || bc2Output[3] != 255)
    {
        error = "Direct3D DXT3 / BC2 texture CPU decoding failed on ARM64";
        return false;
    }

    unsigned char iwiFixture[48 + 16]{};
    iwiFixture[0] = 'I'; iwiFixture[1] = 'W'; iwiFixture[2] = 'i';
    iwiFixture[3] = 13; iwiFixture[4] = 12; iwiFixture[5] = 0x10;
    bo1::bytecode::Store<std::int16_t>(iwiFixture + 6, 4);
    bo1::bytecode::Store<std::int16_t>(iwiFixture + 8, 4);
    bo1::bytecode::Store<std::int16_t>(iwiFixture + 10, 1);
    bo1::bytecode::Store<std::int32_t>(iwiFixture + 16, 64);
    bo1::bytecode::Store<std::int32_t>(iwiFixture + 20, 48);
    std::memcpy(iwiFixture + 48, bc2Block, 16);
    bo1::texture::IwiHeader iwi{};
    std::uint64_t iwiOffset = 0;
    std::size_t iwiSize = 0;
    if (bo1::texture::ParseIwiHeader(iwiFixture, 48, sizeof(iwiFixture), iwi)
            != bo1::texture::IwiParseResult::Ok
        || !bo1::texture::IwiBC2TopMipRange(iwi, iwiOffset, iwiSize)
        || iwiOffset != 48 || iwiSize != 16
        || !bo1::texture::DecodeBC2RGBA(
            iwiFixture + iwiOffset, iwiSize, iwi.width, iwi.height, bc2Output)
        || bc2Output.size() != 64 || bc2Output[0] != 255
        || bc2Output[3] != 255)
    {
        error = "Version 13 IWi image and BC2 highest-resolution mip failed on ARM64";
        return false;
    }


    // Exercise the very same IWI -> UIKit bridge used by the document picker,
    // with a tiny synthetic image instead of any copyrighted game asset.
    NSString* iwiPath = [NSTemporaryDirectory() stringByAppendingPathComponent:
        [NSString stringWithFormat:@"bo1-iwi-test-%@.iwi", NSUUID.UUID.UUIDString]];
    NSData* iwiBytes = [NSData dataWithBytes:iwiFixture length:sizeof(iwiFixture)];
    NSString* previewDetails = nil;
    UIImage* preview = nil;
    if ([iwiBytes writeToFile:iwiPath atomically:YES])
        preview = IwiPreviewForPath(iwiPath.fileSystemRepresentation, &previewDetails);
    [NSFileManager.defaultManager removeItemAtPath:iwiPath error:nullptr];
    if (!preview || preview.size.width != 4 || preview.size.height != 4
        || preview.CGImage == nil || CGImageGetBitsPerPixel(preview.CGImage) != 32
        || previewDetails.length == 0)
    {
        error = "IWI file-to-UIKit RGBA8 preview failed on ARM64";
        return false;
    }

    SCNScene* scene = SceneForMesh(*SyntheticCrossedPlanes(), preview);
    if (!scene || ![scene.rootNode childNodeWithName:@"BO1 preview camera"
        recursively:NO] || !scene.rootNode.childNodes.firstObject.geometry ||
        scene.rootNode.childNodes.firstObject.geometry.sources.count != 2 ||
        scene.rootNode.childNodes.firstObject.geometry.elements.count != 1)
    {
        error = "Synthetic textured LOD0 SceneKit geometry failed on ARM64";
        return false;
    }

    SteamErrMsg steamError{};
    if (SteamAPI_InitEx(&steamError) != k_ESteamAPIInitResult_NoSteamClient ||
        SteamUser() != nullptr || SteamApps() != nullptr || SteamAPI_GetHSteamUser() != 0)
    {
        error = "Offline service backend returned an unexpected state";
        return false;
    }
    SteamAPI_RunCallbacks();
    SteamAPI_Shutdown();
    return true;
}

static bool CheckSyntheticFastfile(std::string& error)
{
    // Synthetic envelope, three string slots and two null asset entries.
    // Contains no game data. It exercises the same path used by the picker.
    const unsigned char fixture[] = {
        0x49,0x57,0x66,0x66,0x75,0x31,0x30,0x30,0xD9,0x01,0x00,0x00,
        0x78,0x9C,0xB3,0x62,0xC0,0x0E,0x1C,0x90,0xD8,0xCC,0x40,0xFC,
        0x1F,0x08,0x98,0xA0,0x34,0x03,0x94,0x06,0x81,0x8C,0xD4,0x9C,
        0x9C,0x7C,0x86,0xAA,0xFC,0xDC,0xA4,0xCC,0xD4,0x62,0x06,0x15,
        0xA8,0x0E,0x55,0x28,0x0D,0x00,0x39,0xEE,0x15,0xC6
    };
    NSString* filename = [NSString stringWithFormat:@"bo1-self-test-%@.ff", NSUUID.UUID.UUIDString];
    NSString* path = [NSTemporaryDirectory() stringByAppendingPathComponent:filename];
    NSError* writeError = nil;
    NSData* data = [NSData dataWithBytes:fixture length:sizeof(fixture)];
    if (![data writeToFile:path options:NSDataWritingAtomic error:&writeError])
    {
        error = "Could not write the synthetic fastfile fixture";
        return false;
    }

    bo1::fastfile::Inspection inspection{};
    const auto result = bo1::fastfile::InspectFile(path.fileSystemRepresentation, inspection, error);
    NSError* removeError = nil;
    const BOOL removed = [NSFileManager.defaultManager removeItemAtPath:path error:&removeError];
    if (result != bo1::fastfile::InspectionResult::Ok)
        return false;
    if (!removed)
    {
        error = "Could not remove the synthetic fastfile fixture";
        return false;
    }
    if (inspection.expandedBytes != 94 || inspection.file.size != 58 ||
        inspection.metadataBytes != 94 || inspection.scriptStrings.size() != 3 ||
        inspection.scriptStrings[0].present || inspection.scriptStrings[1].value != "hello" ||
        inspection.scriptStrings[2].value != "zombies" || inspection.assets.size() != 2 ||
        inspection.assets[0].type != 36 || inspection.assets[1].type != 37 ||
        inspection.assets[0].serializedTag != 0 || inspection.assets[1].serializedTag != 0)
    {
        error = "Synthetic fastfile metadata did not match its expected values";
        return false;
    }
    return true;
}

static NSString* InspectionTextForPath(const char* path, BOOL* success,
    std::shared_ptr<bo1::model::Mesh>* capturedMesh = nullptr)
{
    bo1::fastfile::AssetPrefix prefix{};
    bo1::fastfile::AssetLoadLimits limits{};
    limits.inspection.maxFileBytes = 512ull * 1024 * 1024;
    limits.inspection.maxExpandedBytes = 512ull * 1024 * 1024;
    limits.inspection.maxMetadataBytes = 8 * 1024 * 1024;
    limits.maxNativeBytes = 32 * 1024 * 1024;
    std::string error;
    const auto result = bo1::fastfile::LoadAssetPrefix(path, prefix, error, limits);
    *success = result == bo1::fastfile::InspectionResult::Ok;
    if (capturedMesh) *capturedMesh = nullptr;
    if (!*success)
    {
        NSString* detail = [NSString stringWithUTF8String:error.c_str()];
        return [NSString stringWithFormat:@"No se pudo validar el archivo.\n\n%@",
            detail ?: @"Error de lectura desconocido."];
    }

    const auto& inspection = prefix.inspection;
    size_t populatedStrings = 0;
    for (const auto& string : inspection.scriptStrings)
        populatedStrings += string.present;
    NSMutableString* text = [NSMutableString stringWithFormat:
        @"Estructura validada\n\nVersión: %u\nRecursos: %zu\nCadenas presentes: %zu de %zu\n"
         "Datos descomprimidos: %.2f MiB\nMetadatos leídos: %.2f KiB\n\nTipos de recursos\n",
        inspection.header.version, inspection.assets.size(), populatedStrings,
        inspection.scriptStrings.size(), inspection.expandedBytes / (1024.0 * 1024.0),
        inspection.metadataBytes / 1024.0];
    std::map<uint32_t, size_t> counts;
    for (const auto& asset : inspection.assets)
        ++counts[asset.type];
    for (const auto& entry : counts)
        [text appendFormat:@"%s: %zu\n", bo1::fastfile::AssetTypeName(entry.first), entry.second];

    size_t rawfiles = 0;
    size_t tables = 0;
    size_t cells = 0;
    size_t localizations = 0;
    size_t techsets = 0;
    size_t emptyRoots = 0;
    for (const auto& entry : prefix.entries)
    {
        rawfiles += entry.rawfile != nullptr;
        localizations += entry.localize != nullptr;
        techsets += entry.techniqueSet != nullptr;
        if (entry.stringTable != nullptr)
        {
            ++tables;
            if (entry.stringTable->values != nullptr)
                cells += static_cast<size_t>(entry.stringTable->rowCount) *
                    static_cast<size_t>(entry.stringTable->columnCount);
        }
        emptyRoots += entry.rawfile == nullptr && entry.stringTable == nullptr
            && entry.localize == nullptr && entry.techniqueSet == nullptr;
    }
    [text appendFormat:@"\nRecursos reconstruidos\nArchivos: %zu\nTablas: %zu\nCeldas: %zu\n"
        "Entradas localizadas: %zu\nConjuntos de shaders: %zu\nReferencias vacías: %zu\nProcesados: %zu de %zu\nMemoria contabilizada: %.2f KiB\n",
        rawfiles, tables, cells, localizations, techsets, emptyRoots, prefix.entries.size(),
        inspection.assets.size(), prefix.nativeBytes / 1024.0];
    if (prefix.stop == bo1::fastfile::AssetStop::Complete)
        [text appendString:@"\nReconstrucción del directorio completada.\n"];
    else
    {
        const char* type = prefix.nextAsset < inspection.assets.size()
            ? bo1::fastfile::AssetTypeName(inspection.assets[prefix.nextAsset].type) : "desconocido";
        NSString* reason = prefix.stop == bo1::fastfile::AssetStop::UnsupportedType
            ? @"tipo aún no soportado"
            : (prefix.stop == bo1::fastfile::AssetStop::UnsupportedModelBody
                ? @"modelo identificado; sus recursos internos aún no se cargan"
                : @"referencia aún no resuelta");
        [text appendFormat:@"\nReconstrucción parcial. Detenida en índice %zu (base 0), %s: %@.\n"
            "Los recursos siguientes todavía no se reconstruyeron.\n", prefix.nextAsset, type, reason];
    }
    if (prefix.pendingXModel)
    {
        const auto& model = *prefix.pendingXModel;
        const auto fixed = bo1::fastfile::CalculateXModelInlineBytes(model);
        [text appendFormat:@"\nPrimer XModel pendiente\nHuesos: %u (raíz: %u)\n"
            "Superficies: %u\nLOD: %d\nColisiones: %d\n"
            "Arreglos internos fijos estimados: %.2f KiB\n"
            "Esta información proviene del encabezado; no incluye texturas, "
            "materiales ni geometría descomprimida.\n",
            static_cast<unsigned>(model.numBones),
            static_cast<unsigned>(model.numRootBones),
            static_cast<unsigned>(model.numSurfaces),
            static_cast<int>(model.numLods),
            static_cast<int>(model.numCollisionSurfaces),
            fixed.Total() / 1024.0];
        if (prefix.pendingXModelSkeleton)
        {
            const auto& skel = *prefix.pendingXModelSkeleton;
            NSString* displayName = skel.name.empty() ? @"(no disponible)"
                : [NSString stringWithUTF8String:skel.name.c_str()];
            if (!displayName) displayName = @"(nombre no UTF-8)";
            [text appendFormat:@"Nombre: %@\nBytes de esqueleto leídos: %llu\n"
                "Huesos con identificadores locales: %zu\n"
                "Hijos con referencias externas: 0x%X\n",
                displayName,
                static_cast<unsigned long long>(skel.inlineArrayBytes),
                skel.boneNameBytes.size() / 2, skel.unresolvedFields];
            [text appendFormat:@"Cabeceras XSurface: %zu\n"
                "Vértices declarados: %llu\nTriángulos declarados: %llu\n",
                skel.surfaceHeaders.size(),
                static_cast<unsigned long long>(skel.declaredVertices),
                static_cast<unsigned long long>(skel.declaredTriangles)];
            [text appendFormat:@"Bytes de geometría reconstruidos en CPU: %llu\n",
                static_cast<unsigned long long>(skel.decodedGeometryBytes)];
            [text appendFormat:@"Referencias a materiales: %zu (incrustadas: %u, compartidas: %u, vacías: %u)\n",
                skel.materialHandleTags.size(), skel.inlineMaterials,
                skel.referencedMaterials, skel.emptyMaterialSlots];
        }
    }
    [text appendString:@"\nLos shaders originales no se ejecutan en la GPU. El contenido todavía no está integrado en una partida."];
    if (capturedMesh)
    {
        bo1::model::Mesh mesh;
        std::string reason;
        if (bo1::model::ExtractLOD0(prefix, mesh, reason))
        {
            [text appendFormat:@"\n\nMalla XModel LOD0 disponible: %s\n"
                "Superficies: %zu | Vértices: %zu | Triángulos: %zu\n"
                "Se usa la textura IWI seleccionada manualmente en todas las superficies.",
                mesh.name.c_str(), mesh.surfaceCount,
                mesh.vertices.size(), mesh.indices.size() / 3];
            *capturedMesh = std::make_shared<bo1::model::Mesh>(std::move(mesh));
        }
        else
            [text appendFormat:@"\n\nVista 3D no disponible: %s", reason.c_str()];
    }
    return text;
}

@interface BO1ViewController : UIViewController <UIDocumentPickerDelegate>
{
    std::shared_ptr<bo1::model::Mesh> _modelMesh;
}
@property(nonatomic, strong) UIButton* selectButton;
@property(nonatomic, strong) UIButton* selectIwiButton;
@property(nonatomic, strong) UIImageView* textureView;
@property(nonatomic, strong) SCNView* sceneView;
@property(nonatomic, strong) UIImage* modelTexture;
@property(nonatomic, strong) UIButton* togglePreviewButton;
@property(nonatomic) BOOL showModel;
@property(nonatomic, strong) UILabel* stateLabel;
@property(nonatomic, strong) UITextView* resultView;
@property(nonatomic, strong) UIActivityIndicatorView* activity;
@property(nonatomic, strong) NSURL* iwiFixtureURL;
@property(nonatomic) BOOL iwiFixtureStarted;
- (void)finishIwiFixture:(BOOL)success;
- (void)refresh3DPreview;
@end

@implementation BO1ViewController
- (void)viewDidLoad
{
    [super viewDidLoad];
    self.view.backgroundColor = UIColor.systemBackgroundColor;
    self.title = @"BO1 iOS";

    UILabel* title = [[UILabel alloc] init];
    title.text = @"BO1 iOS — desarrollo";
    title.font = [UIFont preferredFontForTextStyle:UIFontTextStyleTitle2];
    title.adjustsFontForContentSizeCategory = YES;
    title.numberOfLines = 0;

    UILabel* detail = [[UILabel alloc] init];
    detail.text = @"Esta etapa comprueba archivos del juego. Todavía no inicia una partida.";
    detail.font = [UIFont preferredFontForTextStyle:UIFontTextStyleSubheadline];
    detail.textColor = UIColor.secondaryLabelColor;
    detail.adjustsFontForContentSizeCategory = YES;
    detail.numberOfLines = 0;

    self.selectButton = [UIButton buttonWithType:UIButtonTypeSystem];
    UIButtonConfiguration* buttonConfiguration = [UIButtonConfiguration filledButtonConfiguration];
    buttonConfiguration.title = @"Seleccionar archivo .ff";
    buttonConfiguration.cornerStyle = UIButtonConfigurationCornerStyleMedium;
    self.selectButton.configuration = buttonConfiguration;
    [self.selectButton addTarget:self action:@selector(selectFastfile:) forControlEvents:UIControlEventTouchUpInside];

    self.selectIwiButton = [UIButton buttonWithType:UIButtonTypeSystem];
    UIButtonConfiguration* iwiButton = [UIButtonConfiguration tintedButtonConfiguration];
    iwiButton.title = @"Abrir textura .iwi";
    iwiButton.cornerStyle = UIButtonConfigurationCornerStyleMedium;
    self.selectIwiButton.configuration = iwiButton;
    [self.selectIwiButton addTarget:self action:@selector(selectIwi:)
        forControlEvents:UIControlEventTouchUpInside];

    self.textureView = [[UIImageView alloc] init];
    self.textureView.contentMode = UIViewContentModeScaleAspectFit;
    self.textureView.backgroundColor = UIColor.secondarySystemBackgroundColor;
    self.textureView.clipsToBounds = YES;
    self.textureView.layer.cornerRadius = 12;
    self.textureView.accessibilityIdentifier = @"iwiTexturePreview";
    self.textureView.hidden = YES;
    NSLayoutConstraint* imageHeight =
        [self.textureView.heightAnchor constraintEqualToConstant:220];
    // Allow UIStackView to collapse its hidden arranged subview without
    // creating an unsatisfiable required height constraint.
    imageHeight.priority = UILayoutPriorityDefaultHigh;
    imageHeight.active = YES;

    self.sceneView = [[SCNView alloc] init];
    self.sceneView.backgroundColor = UIColor.secondarySystemBackgroundColor;
    self.sceneView.layer.cornerRadius = 12;
    self.sceneView.clipsToBounds = YES;
    self.sceneView.allowsCameraControl = YES;
    self.sceneView.accessibilityIdentifier = @"bo1Model3DPreview";
    self.sceneView.hidden = YES;
    NSLayoutConstraint* sceneHeight =
        [self.sceneView.heightAnchor constraintEqualToConstant:300];
    sceneHeight.priority = UILayoutPriorityDefaultHigh;
    sceneHeight.active = YES;

    self.togglePreviewButton = [UIButton buttonWithType:UIButtonTypeSystem];
    UIButtonConfiguration* toggleConfig = [UIButtonConfiguration plainButtonConfiguration];
    toggleConfig.title = @"Ver textura plana";
    self.togglePreviewButton.configuration = toggleConfig;
    self.togglePreviewButton.hidden = YES;
    [self.togglePreviewButton addTarget:self action:@selector(toggleModelPreview:)
        forControlEvents:UIControlEventTouchUpInside];

    self.stateLabel = [[UILabel alloc] init];
    self.stateLabel.font = [UIFont preferredFontForTextStyle:UIFontTextStyleFootnote];
    self.stateLabel.adjustsFontForContentSizeCategory = YES;
    self.stateLabel.numberOfLines = 0;
    std::string nativeError;
    const bool nativeReady = CheckNativeComponents(nativeError);
    self.stateLabel.text = nativeReady ? @"Componentes nativos listos." : @"Falló la comprobación de los componentes.";
    self.selectButton.enabled = nativeReady;
    self.selectIwiButton.enabled = nativeReady;

    self.activity = [[UIActivityIndicatorView alloc] initWithActivityIndicatorStyle:UIActivityIndicatorViewStyleMedium];
    self.activity.hidesWhenStopped = YES;
    UIStackView* status = [[UIStackView alloc] initWithArrangedSubviews:@[self.stateLabel, self.activity]];
    status.axis = UILayoutConstraintAxisHorizontal;
    status.spacing = 8;

    self.resultView = [[UITextView alloc] init];
    self.resultView.editable = NO;
    self.resultView.selectable = YES;
    self.resultView.font = [UIFont monospacedSystemFontOfSize:14 weight:UIFontWeightRegular];
    self.resultView.backgroundColor = UIColor.secondarySystemBackgroundColor;
    self.resultView.layer.cornerRadius = 12;
    self.resultView.textContainerInset = UIEdgeInsetsMake(12, 8, 12, 8);
    self.resultView.accessibilityIdentifier = @"fastfileResult";
    self.resultView.text = nativeReady
        ? @"Seleccioná un archivo .ff desde Archivos para comprobar su estructura.\n\nMemoria de 64 bits verificada.\nServicios Steam inactivos."
        : [NSString stringWithUTF8String:nativeError.c_str()];

    UIStackView* content = [[UIStackView alloc] initWithArrangedSubviews:
        @[title, detail, self.selectButton, self.selectIwiButton,
          status, self.sceneView, self.textureView, self.togglePreviewButton,
          self.resultView]];
    content.axis = UILayoutConstraintAxisVertical;
    content.spacing = 12;
    content.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:content];
    UILayoutGuide* safe = self.view.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [content.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor constant:20],
        [content.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor constant:-20],
        [content.topAnchor constraintEqualToAnchor:safe.topAnchor constant:16],
        [content.bottomAnchor constraintEqualToAnchor:safe.bottomAnchor constant:-16]
    ]];
    [self.resultView setContentHuggingPriority:UILayoutPriorityDefaultLow - 1 forAxis:UILayoutConstraintAxisVertical];
    if (showFixturePreview)
    {
        // CI-only launch flag: display the synthetic native assets to inspect
        // the results UI without shipping or uploading any game content.
        NSString* path = [NSTemporaryDirectory() stringByAppendingPathComponent:
            [NSString stringWithFormat:@"bo1-preview-%@.ff", NSUUID.UUID.UUIDString]];
        const auto bytes = bo1::tests::TechsetFixture();
        NSData* fixture = [NSData dataWithBytes:bytes.data() length:bytes.size()];
        BOOL success = NO;
        if ([fixture writeToFile:path atomically:YES])
            self.resultView.text = InspectionTextForPath(path.fileSystemRepresentation, &success);
        [NSFileManager.defaultManager removeItemAtPath:path error:nullptr];
        self.stateLabel.text = success ? @"Prueba con datos sintéticos completada." : @"Falló la prueba sintética.";
    }
}

- (void)viewDidAppear:(BOOL)animated
{
    [super viewDidAppear:animated];
    if ((!showIwiFixturePreview && !showModelFixturePreview) || self.iwiFixtureStarted) return;
    self.iwiFixtureStarted = YES;
    // A fixed white backing makes the alpha-compositing assertion deterministic
    // in either appearance. The normal document picker retains its styling.
    self.textureView.backgroundColor = UIColor.whiteColor;
    if (showModelFixturePreview)
    {
        _modelMesh = SyntheticCrossedPlanes();
        self.showModel = YES;
    }
    self.iwiFixtureURL = [NSURL fileURLWithPath:[NSTemporaryDirectory()
        stringByAppendingPathComponent:@"bo1-synthetic-preview.iwi"]];
    if (![IwiPreviewFixture() writeToURL:self.iwiFixtureURL atomically:YES])
    {
        [self finishIwiFixture:NO];
        return;
    }
    // Exercise the same coordinated background read and UI update as a picked
    // document. The picker interaction itself is a separate manual test.
    [self documentPicker:nil didPickDocumentsAtURLs:@[self.iwiFixtureURL]];
}

- (void)finishIwiFixture:(BOOL)success
{
    [NSFileManager.defaultManager removeItemAtURL:self.iwiFixtureURL error:nullptr];
    self.iwiFixtureURL = nil;
    dispatch_async(dispatch_get_main_queue(), ^{
        [self.view.window layoutIfNeeded];
        NSDictionary* report = success ? IwiFixtureLayoutReport(self.textureView)
            : @{@"status": @"fail", @"error": self.resultView.text ?: @"Fixture read failed"};
        NSString* markerName = @"bo1-iwi-preview.json";
        if (showModelFixturePreview)
        {
            const bool rendered = success && self.sceneView.scene &&
                !self.sceneView.hidden && self.sceneView.pointOfView &&
                self.sceneView.scene.rootNode.childNodes.firstObject.geometry &&
                self.sceneView.bounds.size.width > 100 &&
                self.sceneView.bounds.size.height > 100;
            report = rendered
                ? @{@"status": @"pass", @"renderer": @"SceneKit",
                    @"fixture_vertices": @8, @"fixture_triangles": @4,
                    @"surface_count": @2,
                    @"height": @(self.sceneView.bounds.size.height),
                    @"width": @(self.sceneView.bounds.size.width)}
                : @{@"status": @"fail", @"error": @"SceneKit model preview not visible"};
            markerName = @"bo1-model-preview.json";
        }
        NSData* json = [NSJSONSerialization dataWithJSONObject:report
            options:NSJSONWritingPrettyPrinted error:nullptr];
        NSString* marker = [NSTemporaryDirectory() stringByAppendingPathComponent:markerName];
        if (![json writeToFile:marker atomically:YES])
            NSLog(@"Could not write IWI preview marker");
    });
}

- (void)refresh3DPreview
{
    const bool ready = _modelMesh && self.modelTexture;
    self.togglePreviewButton.hidden = !ready;
    if (!ready)
    {
        self.sceneView.hidden = YES;
        self.sceneView.scene = nil;
        self.textureView.hidden = self.textureView.image == nil;
        return;
    }
    if (!self.sceneView.scene)
    {
        SCNScene* scene = SceneForMesh(*_modelMesh, self.modelTexture);
        self.sceneView.scene = scene;
        SCNNode* camera = [scene.rootNode childNodeWithName:@"BO1 preview camera"
            recursively:NO];
        self.sceneView.pointOfView = camera;
    }
    const BOOL show3D = self.showModel;
    self.sceneView.hidden = !show3D;
    self.textureView.hidden = show3D;
    UIButtonConfiguration* config = self.togglePreviewButton.configuration;
    config.title = show3D ? @"Ver textura plana" : @"Ver modelo 3D";
    self.togglePreviewButton.configuration = config;
    if (show3D)
        self.stateLabel.text = @"Vista 3D: XModel real + textura IWI (giralo con el dedo).";
}

- (void)toggleModelPreview:(id)sender
{
    self.showModel = !self.showModel;
    [self refresh3DPreview];
}

- (void)selectFastfile:(id)sender
{
    UTType* type = [UTType typeWithIdentifier:@"com.antipublico.bo1-fastfile"];
    UIDocumentPickerViewController* picker = [[UIDocumentPickerViewController alloc]
        initForOpeningContentTypes:@[type ?: UTTypeData] asCopy:NO];
    picker.delegate = self;
    picker.allowsMultipleSelection = NO;
    [self presentViewController:picker animated:YES completion:nil];
}

- (void)selectIwi:(id)sender
{
    // Generic data type permits an unregistered .iwi from Files, including
    // local copies imported from iCloud Drive or a Mac. Validate the suffix
    // and the complete 48-byte IWi header before using it.
    UTType* type = [UTType typeWithIdentifier:@"com.antipublico.bo1-iwi"];
    UIDocumentPickerViewController* picker = [[UIDocumentPickerViewController alloc]
        initForOpeningContentTypes:@[type ?: UTTypeData] asCopy:NO];
    picker.delegate = self;
    picker.allowsMultipleSelection = NO;
    [self presentViewController:picker animated:YES completion:nil];
}

- (void)documentPicker:(UIDocumentPickerViewController*)controller didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls
{
    NSURL* url = urls.firstObject;
    if (url == nil)
        return;
    const BOOL isIwi = [url.pathExtension caseInsensitiveCompare:@"iwi"] == NSOrderedSame;
    const BOOL isFastfile = [url.pathExtension caseInsensitiveCompare:@"ff"] == NSOrderedSame;
    if (!isIwi && !isFastfile)
    {
        self.stateLabel.text = @"Seleccioná un archivo .ff o .iwi.";
        return;
    }

    self.selectButton.enabled = NO;
    self.selectIwiButton.enabled = NO;
    if (isIwi)
    {
        self.textureView.image = nil;
        self.modelTexture = nil;
    }
    else
        _modelMesh.reset();
    self.sceneView.scene = nil;
    self.sceneView.hidden = YES;
    self.togglePreviewButton.hidden = YES;
    self.textureView.hidden = self.textureView.image == nil;
    self.stateLabel.text = isIwi ? @"Decodificando textura DXT3…" :
        @"Leyendo y comprobando el archivo…";
    self.resultView.text = url.lastPathComponent;
    [self.activity startAnimating];
    // Keep access and coordination alive for the complete background read.
    // https://developer.apple.com/documentation/uikit/providing-access-to-directories
    const BOOL scoped = [url startAccessingSecurityScopedResource];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        @autoreleasepool
        {
            __block NSString* text = nil;
            __block UIImage* preview = nil;
            __block std::shared_ptr<bo1::model::Mesh> capturedMesh;
            __block BOOL success = NO;
            NSError* coordinationError = nil;
            NSFileCoordinator* coordinator = [[NSFileCoordinator alloc] initWithFilePresenter:nil];
            [coordinator coordinateReadingItemAtURL:url options:NSFileCoordinatorReadingWithoutChanges
                error:&coordinationError byAccessor:^(NSURL* coordinatedURL) {
                    if (isIwi)
                    {
                        preview = IwiPreviewForPath(coordinatedURL.fileSystemRepresentation, &text);
                        success = preview != nil;
                    }
                    else
                        text = InspectionTextForPath(coordinatedURL.fileSystemRepresentation,
                            &success, &capturedMesh);
                }];
            if (scoped)
                [url stopAccessingSecurityScopedResource];
            if (coordinationError != nil || text == nil)
            {
                success = NO;
                text = [NSString stringWithFormat:@"No se pudo acceder al archivo.\n\n%@",
                    coordinationError.localizedDescription ?: @"No hay acceso al archivo seleccionado."];
            }
            dispatch_async(dispatch_get_main_queue(), ^{
                self.resultView.text = text;
                if (isIwi)
                {
                    self.modelTexture = success ? preview : nil;
                    self.textureView.image = self.modelTexture;
                }
                else
                    _modelMesh = success ? capturedMesh : nullptr;
                self.showModel = _modelMesh && self.modelTexture;
                self.stateLabel.text = success
                    ? (isIwi ? @"Textura DXT3 reconstruida en iOS." : @"Comprobación completada.")
                    : (isIwi ? @"No se pudo abrir la textura." : @"El archivo no pudo validarse.");
                self.selectButton.enabled = YES;
                self.selectIwiButton.enabled = YES;
                [self.activity stopAnimating];
                [self refresh3DPreview];
                if ((showIwiFixturePreview || showModelFixturePreview) && self.iwiFixtureURL)
                {
                    if (success) self.stateLabel.text = @"Textura sintética DXT3: colores y transparencia.";
                    [self finishIwiFixture:success];
                }
            });
        }
    });
}
@end

@interface BO1SceneDelegate : UIResponder <UIWindowSceneDelegate>
@property(nonatomic, strong) UIWindow* window;
@end

@implementation BO1SceneDelegate
- (void)scene:(UIScene*)scene willConnectToSession:(UISceneSession*)session options:(UISceneConnectionOptions*)options
{
    if (![scene isKindOfClass:UIWindowScene.class])
        return;
    self.window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene*)scene];
    self.window.rootViewController = [[BO1ViewController alloc] init];
    [self.window makeKeyAndVisible];

}
@end

@interface BO1AppDelegate : UIResponder <UIApplicationDelegate>
@end

@implementation BO1AppDelegate
- (BOOL)application:(UIApplication*)application didFinishLaunchingWithOptions:(NSDictionary*)launchOptions
{
    // Run CI tests when the application has launched, without waiting for a
    // UIWindowScene. Headless simulators do not always connect a scene.
    if (runSelfTest)
    {
        dispatch_async(dispatch_get_main_queue(), ^{
            std::string error;
            NSString* filename = [NSString stringWithFormat:@"bo1-native-test-%@.ff", NSUUID.UUID.UUIDString];
            NSString* path = [NSTemporaryDirectory() stringByAppendingPathComponent:filename];
            const bool passed = CheckNativeComponents(error) && CheckSyntheticFastfile(error) &&
                bo1::tests::CheckNativeAssetFixture(path.fileSystemRepresentation, error) &&
                bo1::tests::CheckTechsetFixture(path.fileSystemRepresentation, error);
            // simctl --console may miss stdout if the app exits before the
            // simulator connects its log pipe. Persist a deterministic result
            // in the app data container before exiting for CI to inspect.
            NSString* marker = [NSTemporaryDirectory() stringByAppendingPathComponent:
                @"bo1-native-self-test.txt"];
            NSString* status = passed ? @"BO1_IOS_SELF_TEST_PASS"
                : [NSString stringWithFormat:@"BO1_IOS_SELF_TEST_FAIL: %s", error.c_str()];
            NSError* markerError = nil;
            const BOOL wroteMarker = [status writeToFile:marker atomically:YES
                encoding:NSUTF8StringEncoding error:&markerError];
            if (passed)
                std::puts("BO1_IOS_SELF_TEST_PASS");
            else
                std::fprintf(stderr, "BO1_IOS_SELF_TEST_FAIL: %s\n", error.c_str());
            if (!wroteMarker)
                std::fprintf(stderr, "BO1_IOS_SELF_TEST_MARKER_FAIL: %s\n",
                    markerError.localizedDescription.UTF8String);
            std::fflush(stdout);
            std::fflush(stderr);
            std::exit(passed && wroteMarker ? 0 : 1);
        });
    }
    return YES;
}
@end

int main(int argc, char* argv[])
{
    @autoreleasepool
    {
        for (int index = 1; index < argc; ++index)
        {
            runSelfTest = runSelfTest || std::strcmp(argv[index], "--self-test") == 0;
            showFixturePreview = showFixturePreview || std::strcmp(argv[index], "--fixture-preview") == 0;
            showIwiFixturePreview = showIwiFixturePreview || std::strcmp(argv[index], "--fixture-iwi-preview") == 0;
            showModelFixturePreview = showModelFixturePreview || std::strcmp(argv[index], "--fixture-model-preview") == 0;
        }
        return UIApplicationMain(argc, argv, nil, NSStringFromClass(BO1AppDelegate.class));
    }
}

#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <database/db_fastfile_format.h>
#include <database/fastfile_inspection.h>
#include <database/fastfile_assets.h>
#include <database/db_xmodel_header.h>
#include <database/db_xsurface_header.h>
#include <platform/bo1_texture_bc.h>
#include <platform/bo1_bytecode_io.h>
#include <clientscript/cscr_bytecode.h>
#include <steam/steam_api.h>
#include <universal/narrow32.h>
#include "../tests/native_asset_fixture.h"
#include "../tests/techset_fixture.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

static bool runSelfTest = false;
static bool showFixturePreview = false;

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

static NSString* InspectionTextForPath(const char* path, BOOL* success)
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
    [text appendString:@"\nLos shaders aún no se ejecutan en la GPU. El contenido todavía no está integrado en una partida."];
    return text;
}

@interface BO1ViewController : UIViewController <UIDocumentPickerDelegate>
@property(nonatomic, strong) UIButton* selectButton;
@property(nonatomic, strong) UILabel* stateLabel;
@property(nonatomic, strong) UITextView* resultView;
@property(nonatomic, strong) UIActivityIndicatorView* activity;
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

    self.stateLabel = [[UILabel alloc] init];
    self.stateLabel.font = [UIFont preferredFontForTextStyle:UIFontTextStyleFootnote];
    self.stateLabel.adjustsFontForContentSizeCategory = YES;
    self.stateLabel.numberOfLines = 0;
    std::string nativeError;
    const bool nativeReady = CheckNativeComponents(nativeError);
    self.stateLabel.text = nativeReady ? @"Componentes nativos listos." : @"Falló la comprobación de los componentes.";
    self.selectButton.enabled = nativeReady;

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
        @[title, detail, self.selectButton, status, self.resultView]];
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

- (void)selectFastfile:(id)sender
{
    UTType* type = [UTType typeWithIdentifier:@"com.antipublico.bo1-fastfile"];
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
    if ([url.pathExtension caseInsensitiveCompare:@"ff"] != NSOrderedSame)
    {
        self.stateLabel.text = @"Seleccioná un archivo con extensión .ff.";
        return;
    }

    self.selectButton.enabled = NO;
    self.stateLabel.text = @"Leyendo y comprobando el archivo…";
    self.resultView.text = url.lastPathComponent;
    [self.activity startAnimating];
    // Keep access and coordination alive for the complete background read.
    // https://developer.apple.com/documentation/uikit/providing-access-to-directories
    const BOOL scoped = [url startAccessingSecurityScopedResource];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        @autoreleasepool
        {
            __block NSString* text = nil;
            __block BOOL success = NO;
            NSError* coordinationError = nil;
            NSFileCoordinator* coordinator = [[NSFileCoordinator alloc] initWithFilePresenter:nil];
            [coordinator coordinateReadingItemAtURL:url options:NSFileCoordinatorReadingWithoutChanges
                error:&coordinationError byAccessor:^(NSURL* coordinatedURL) {
                    text = InspectionTextForPath(coordinatedURL.fileSystemRepresentation, &success);
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
                self.stateLabel.text = success ? @"Comprobación completada." : @"El archivo no pudo validarse.";
                self.selectButton.enabled = YES;
                [self.activity stopAnimating];
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
        }
        return UIApplicationMain(argc, argv, nil, NSStringFromClass(BO1AppDelegate.class));
    }
}

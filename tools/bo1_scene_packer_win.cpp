#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <platform/bo1_iwd_zip.h>
#include <algorithm>
#include <cctype>
#include <system_error>
#include <database/fastfile_assets.h>
#include <platform/bo1_model_mesh.h>
#include <platform/bo1_scene_pack.h>
#include <platform/bo1_iwi_image.h>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>
#include <array>
#include <thread>
#include <memory>
#include <map>
#include <cstdio>

// Local Windows GUI; picks user-owned .ff and .iwi. Export contains ONLY
// decoded LOD0 CPU vertices/triangles and one BC2 top mip, not the source files.
namespace {
constexpr UINT Finished = WM_APP + 1;
HWND autoButton=nullptr;
HWND windowHandle=nullptr, statusLabel=nullptr, modelsLabel=nullptr;
HWND addModelButton=nullptr, imageButton=nullptr, saveButton=nullptr, clearButton=nullptr;
std::vector<std::wstring> files;
std::wstring imagePath;
bool busy=false;

std::string Utf8(const std::wstring& w) {
    if(w.empty())return {};
    int n=WideCharToMultiByte(CP_UTF8,0,w.c_str(),int(w.size()),nullptr,0,nullptr,nullptr);
    if(n<=0)return {};
    std::string s(n,'\0');
    WideCharToMultiByte(CP_UTF8,0,w.c_str(),int(w.size()),s.data(),n,nullptr,nullptr);
    return s;
}
std::wstring Wide(const std::string& s) {
    if(s.empty())return {};
    const int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);
    if(n<=0)return L"Error al convertir el nombre del archivo.";
    std::wstring w(n,L'\0');
    MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),w.data(),n);
    return w;
}
std::wstring FileName(const std::wstring& p) {
    return std::filesystem::path(p).filename().wstring();
}
std::wstring Pick(bool save, const wchar_t* filter, const wchar_t* extension) {
    wchar_t name[32768]{};
    OPENFILENAMEW o{};
    o.lStructSize=sizeof(o);
    o.hwndOwner=windowHandle;
    o.lpstrFilter=filter;
    o.nFilterIndex=1;
    o.lpstrFile=name;
    o.nMaxFile=32768;
    o.lpstrDefExt=extension;
    o.Flags=OFN_EXPLORER|OFN_PATHMUSTEXIST|
        (save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
    const BOOL result=save?GetSaveFileNameW(&o):GetOpenFileNameW(&o);
    return result?std::wstring(name):std::wstring();
}
void UpdateLabels() {
    wchar_t info[256];
    swprintf_s(info,L"Modelos .ff: %zu / 6 (uno por archivo)",files.size());
    SetWindowTextW(modelsLabel,info);
    EnableWindow(addModelButton,!busy&&files.size()<bo1::scene_pack::MaxModels);
    EnableWindow(imageButton,!busy);
    EnableWindow(saveButton,!busy&&!files.empty()&&!imagePath.empty());
    EnableWindow(clearButton,!busy);
    if (autoButton) EnableWindow(autoButton,!busy);
}
bool ReadImage(const std::wstring& path, bo1::scene_pack::Package& pack, std::string& error) {
    std::ifstream stream(std::filesystem::path(path),std::ios::binary|std::ios::ate);
    if(!stream){error="No se puede abrir el .iwi";return false;}
    auto end=stream.tellg();
    if(end<std::streamoff(bo1::texture::IwiHeaderBytes)){
        error="IWI demasiado corto";return false;
    }
    std::uint64_t total=static_cast<std::uint64_t>(end);
    std::array<unsigned char,48> bytes{};
    stream.seekg(0,std::ios::beg);
    stream.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
    bo1::texture::IwiHeader header{};
    if(!stream||bo1::texture::ParseIwiHeader(bytes.data(),bytes.size(),total,header)
                   !=bo1::texture::IwiParseResult::Ok){
        error="IWI v13 no válido";return false;
    }
    std::uint64_t offset=0;size_t length=0;
    if(!bo1::texture::IwiBC2TopMipRange(header,offset,length)){
        error="La textura debe ser IWI v13 DXT3/BC2 2D";return false;
    }
    pack.width=header.width;pack.height=header.height;
    pack.bc2.resize(length);
    stream.seekg(static_cast<std::streamoff>(offset),std::ios::beg);
    stream.read(reinterpret_cast<char*>(pack.bc2.data()),length);
    if(!stream){error="Datos BC2 truncados";return false;}
    return true;
}
std::wstring Export(const std::vector<std::wstring>& sources,
                    const std::wstring& iwi,const std::wstring& dst) {
    bo1::scene_pack::Package pack;
    std::string error;
    if(!ReadImage(iwi,pack,error))return L"Error de textura: "+Wide(error);
    for(const auto& filename:sources) {
        bo1::fastfile::AssetPrefix prefix;
        bo1::fastfile::AssetLoadLimits limits;
        const std::string path=Utf8(filename);
        if(bo1::fastfile::LoadAssetPrefix(path.c_str(),prefix,error,limits)
               !=bo1::fastfile::InspectionResult::Ok)
            return L"No se pudo leer "+FileName(filename)+L": "+Wide(error);
        bo1::model::Mesh mesh;
        if(!bo1::model::ExtractLOD0(prefix,mesh,error))
            return L"No se pudo extraer LOD0 de "+FileName(filename)+L": "+Wide(error)+
                L"\nSe admite el primer XModel con geometría inline decodificable.";
        pack.meshes.push_back(std::move(mesh));
    }
    std::vector<std::uint8_t> bytes;
    if(!bo1::scene_pack::Encode(pack,bytes,error))
        return L"No se pudo empaquetar: "+Wide(error);
    bo1::scene_pack::Package verified;
    if(!bo1::scene_pack::Decode(bytes.data(),bytes.size(),verified,error))
        return L"No se pudo verificar el paquete: "+Wide(error);
    std::ofstream out(std::filesystem::path(dst),std::ios::binary|std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    out.close();
    if(!out)return L"No se pudo guardar el archivo .bo1scene";
    wchar_t summary[220];
    swprintf_s(summary,L"ESCENA CREADA\n%zu modelo(s), %ux%u DXT3, %.1f KiB.\nPasala a Archivos en tu iPhone y abrila desde BO1 iOS.",
        verified.meshes.size(),verified.width,verified.height,bytes.size()/1024.);
    return summary;
}

std::wstring ChooseGameFolder() {
    wchar_t display[MAX_PATH]{};
    BROWSEINFOW b{};
    b.hwndOwner=windowHandle;
    b.pszDisplayName=display;
    b.lpszTitle=L"Elegí la carpeta donde está instalado Call of Duty Black Ops";
    b.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pid=SHBrowseForFolderW(&b);
    if(!pid)return {};
    wchar_t location[MAX_PATH]{};
    BOOL ok=SHGetPathFromIDListW(pid,location);
    CoTaskMemFree(pid);
    return ok?std::wstring(location):std::wstring();
}
bool ReadIwiBytes(const std::vector<std::uint8_t>& bytes,
                  bo1::scene_pack::Package& pack,std::string& error) {
    bo1::texture::IwiHeader image{};
    if(bo1::texture::ParseIwiHeader(bytes.data(),bytes.size(),bytes.size(),image)
        !=bo1::texture::IwiParseResult::Ok) {
        error="IWI v13 inválido";return false;
    }
    std::uint64_t offset=0;size_t length=0;
    if(!bo1::texture::IwiBC2TopMipRange(image,offset,length)
       || length>2u*1024u*1024u) {
        error="Textura IWI sin DXT3 compatible o demasiado grande";return false;
    }
    pack.width=image.width;pack.height=image.height;
    pack.bc2.assign(bytes.begin()+static_cast<size_t>(offset),
                    bytes.begin()+static_cast<size_t>(offset+length));
    return true;
}
bool ReadLooseIwi(const std::filesystem::path& p,
                  bo1::scene_pack::Package& pack,std::string& error) {
    std::ifstream file(p,std::ios::binary|std::ios::ate);
    if(!file)return false;
    auto size=file.tellg();
    if(size<std::streamoff(48)||size>std::streamoff(bo1::iwd::MaxEntryBytes))
        return false;
    std::vector<std::uint8_t> bytes(static_cast<size_t>(size));
    file.seekg(0,std::ios::beg);
    if(!file.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))
        return false;
    return ReadIwiBytes(bytes,pack,error);
}
struct ScanResults {
    std::vector<std::filesystem::path> fastfiles,archives,images;
};
bool ScanFolder(const std::filesystem::path& folder,
                ScanResults& found,std::string& error) {
    found={};error.clear();
    std::error_code ec;
    if(!std::filesystem::is_directory(folder,ec)){
        error="La carpeta seleccionada no existe";return false;
    }
    std::filesystem::recursive_directory_iterator it(
        folder,std::filesystem::directory_options::skip_permission_denied,ec),end;
    std::size_t examined=0;
    for(;it!=end&&!ec;++it){
        if(++examined>40000)break;
        if(it.depth()>6){it.disable_recursion_pending();continue;}
        if(it->is_symlink(ec)||!it->is_regular_file(ec))continue;
        const auto extension=bo1::iwd::Lower(Utf8(it->path().extension().wstring()));
        if(extension==".ff"){
            const auto size=it->file_size(ec);
            if(!ec&&size<=512ull*1024*1024)found.fastfiles.push_back(it->path());
        }else if(extension==".iwd")found.archives.push_back(it->path());
        else if(extension==".iwi")found.images.push_back(it->path());
    }
    if(found.fastfiles.empty()) {
        error="No encontré .ff en esta carpeta. Elegí la raíz de instalación (con zone).";
        return false;
    }
    // Try the smaller maps first to avoid repeatedly decompressing huge zones.
    // Folders called zombie_* are prioritized, but no map placement is inferred.
    std::sort(found.fastfiles.begin(),found.fastfiles.end(),
        [](const auto& a,const auto& b){
            const auto x=bo1::iwd::Lower(Utf8(a.filename().wstring()));
            const auto y=bo1::iwd::Lower(Utf8(b.filename().wstring()));
            const bool za=x.rfind("zombie_",0)==0;
            const bool zb=y.rfind("zombie_",0)==0;
            if(za!=zb)return za;
            std::error_code e1,e2;
            return std::filesystem::file_size(a,e1)<
                   std::filesystem::file_size(b,e2);
        });
    return true;
}

std::wstring AutomaticExport(const std::wstring& folder,
                             const std::wstring& destination) {
    ScanResults found;
    std::string error;
    if(!ScanFolder(folder,found,error))return L"Carpeta: "+Wide(error);
    struct Selected {
        bo1::model::Mesh mesh;
        std::string wanted, source;
        std::size_t vertices=0, triangles=0;
    };
    std::vector<Selected> models;
    std::size_t attempted=0;
    std::size_t estimatedMeshBytes=0;
    for(const auto& file:found.fastfiles) {
        if(++attempted>80 || models.size()>=bo1::scene_pack::MaxModels)break;
        bo1::fastfile::AssetPrefix prefix;
        bo1::fastfile::AssetLoadLimits limits;
        limits.previewFirstMaterialHeader=true;
        limits.previewFirstImageHeader=true;
        const std::string path=Utf8(file.wstring());
        if(bo1::fastfile::LoadAssetPrefix(path.c_str(),prefix,error,limits)
            !=bo1::fastfile::InspectionResult::Ok)continue;
        bo1::model::Mesh mesh;
        if(!bo1::model::ExtractLOD0(prefix,mesh,error))continue;
        if(mesh.vertices.size()>bo1::scene_pack::MaxVerticesPerModel ||
           mesh.indices.size()>bo1::scene_pack::MaxIndicesPerModel)continue;
        const auto amount=16+mesh.name.size()+mesh.vertices.size()*20+
                          mesh.indices.size()*4;
        if(estimatedMeshBytes+amount>9u*1024u*1024u)continue;
        Selected selected{};
        selected.source=Utf8(file.filename().wstring());
        selected.vertices=mesh.vertices.size();
        selected.triangles=mesh.indices.size()/3;
        if(prefix.pendingXModelSkeleton &&
           prefix.pendingXModelSkeleton->firstInlineMaterial &&
           prefix.pendingXModelSkeleton->firstInlineMaterial->firstInlineImage) {
            selected.wanted=bo1::iwd::Lower(
                prefix.pendingXModelSkeleton->firstInlineMaterial->
                firstInlineImage->name);
        }
        selected.mesh=std::move(mesh);
        estimatedMeshBytes+=amount;
        models.push_back(std::move(selected));
    }
    if(models.empty()) {
        return L"Encontré "+std::to_wstring(found.fastfiles.size())+
            L" archivos FF, pero los primeros "+std::to_wstring(attempted)+
            L" no expusieron geometría LOD0 compatible. No inventé modelos.";
    }
    struct Candidate {
        std::string name, key;
        std::filesystem::path archive, loose;
        bo1::iwd::Entry entry;
        std::uint32_t bytes=0;
    };
    std::vector<Candidate> candidates;
    for(const auto& img:found.images) {
        std::error_code ec;
        const auto n=std::filesystem::file_size(img,ec);
        if(!ec&&n<=2u*1024u*1024u) {
            Candidate c{};
            c.name=bo1::iwd::Lower(Utf8(img.filename().wstring()));
            c.key=Utf8(img.wstring());
            c.loose=img;
            c.bytes=static_cast<std::uint32_t>(n);
            candidates.push_back(std::move(c));
        }
    }
    // Reading IWD directories does not extract anything to disk. Only the
    // texture ultimately used in the pack is decompressed, with CRC checking.
    for(const auto& archive:found.archives) {
        std::vector<bo1::iwd::Entry> images;
        if(!bo1::iwd::Index(archive,images,error))continue;
        for(const auto& e:images) {
            if(e.original>2u*1024u*1024u)continue;
            Candidate c{};
            c.name=e.name;
            c.key=Utf8(archive.wstring())+":"+e.name;
            c.archive=archive;
            c.entry=e;
            c.bytes=e.original;
            candidates.push_back(std::move(c));
        }
    }
    if(candidates.empty()) {
        return L"Encontré "+std::to_wstring(models.size())+
               L" modelos, pero no hay texturas .iwi elegibles en la carpeta/.iwd.";
    }
    bo1::scene_pack::Package pack;
    std::map<std::string,std::uint32_t> textureIds;
    std::vector<std::string> chosenTextures;
    unsigned nameMatches=0, fallbackMatches=0;
    std::size_t totalTextureBytes=0;
    auto score=[&](const Selected& mesh,const Candidate& image)->int{
        const std::string desired=bo1::iwd::Lower(mesh.wanted);
        const std::string name=bo1::iwd::Lower(mesh.mesh.name);
        if(!desired.empty()) {
            const auto slash=desired.find_last_of('/');
            const std::string leaf=slash==std::string::npos?desired:desired.substr(slash+1);
            if(image.name==desired+".iwi" || image.name=="images/"+desired+".iwi"
               || image.name==desired || image.name=="images/"+desired)
                return 100;
            if(!leaf.empty() && image.name.find(leaf)!=std::string::npos)
                return 95;
        }
        if(!name.empty()&&image.name.find(name)!=std::string::npos)return 65;
        return 0;
    };
    for(const auto& selected:models) {
        std::vector<std::size_t> ranked(candidates.size());
        for(std::size_t i=0;i<ranked.size();++i)ranked[i]=i;
        std::sort(ranked.begin(),ranked.end(),[&](std::size_t a,std::size_t b){
            const auto pa=score(selected,candidates[a]);
            const auto pb=score(selected,candidates[b]);
            if(pa!=pb)return pa>pb;
            const auto alreadyA=textureIds.count(candidates[a].key)!=0;
            const auto alreadyB=textureIds.count(candidates[b].key)!=0;
            if(alreadyA!=alreadyB)return alreadyA;
            return candidates[a].bytes<candidates[b].bytes;
        });
        bool chosen=false;
        std::uint32_t slot=0;
        for(std::size_t pass=0;pass<ranked.size()&&pass<160;++pass) {
            const auto& candidate=candidates[ranked[pass]];
            const auto matchScore=score(selected,candidate);
            if(models.size()>1 && !selected.wanted.empty() &&
               matchScore<90 && !textureIds.empty()) break; // do not guess a different texture
            const auto existing=textureIds.find(candidate.key);
            if(existing!=textureIds.end()) {
                slot=existing->second;
                chosen=true;
            }else if(textureIds.size()<bo1::scene_pack::MaxModels &&
                     totalTextureBytes+candidate.bytes<6u*1024u*1024u) {
                bo1::scene_pack::Package image;
                bool good=false;
                if(!candidate.loose.empty()) {
                    good=ReadLooseIwi(candidate.loose,image,error);
                }else {
                    std::vector<std::uint8_t> iwi;
                    good=bo1::iwd::Read(candidate.archive,candidate.entry,iwi,error) &&
                         ReadIwiBytes(iwi,image,error);
                }
                if(!good||totalTextureBytes+image.bc2.size()>6u*1024u*1024u)
                    continue;
                slot=static_cast<std::uint32_t>(textureIds.size());
                if(slot==0) {
                    pack.width=image.width;
                    pack.height=image.height;
                    pack.bc2=std::move(image.bc2);
                }else {
                    bo1::scene_pack::Texture tex{};
                    tex.width=image.width;tex.height=image.height;
                    tex.bc2=std::move(image.bc2);
                    pack.extraTextures.push_back(std::move(tex));
                }
                totalTextureBytes+=slot==0?pack.bc2.size():pack.extraTextures.back().bc2.size();
                textureIds.emplace(candidate.key,slot);
                chosenTextures.push_back(candidate.name);
                chosen=true;
            }
            if(chosen) {
                if(matchScore>=90)++nameMatches;
                else ++fallbackMatches;
                break;
            }
        }
        if(!chosen) {
            if(textureIds.empty())continue;
            // A source with no resolved material uses the first known texture.
            // This fallback is always reported as unverified in the GUI.
            slot=0;
            ++fallbackMatches;
        }
        pack.meshes.push_back(selected.mesh);
        pack.textureSlots.push_back(slot);
    }
    if(pack.meshes.empty() || pack.bc2.empty())
        return L"Los modelos fueron encontrados pero ninguna textura DXT3 válida"
               L" pudo extraerse de los .iwd/.iwi. No se creó el paquete.";
    std::vector<std::uint8_t> output;
    if(!bo1::scene_pack::Encode(pack,output,error))
        return L"El paquete supera los límites de tamaño: "+Wide(error);
    bo1::scene_pack::Package verified;
    if(!bo1::scene_pack::Decode(output.data(),output.size(),verified,error))
        return L"Error validando el paquete: "+Wide(error);
    std::ofstream file(std::filesystem::path(destination),std::ios::binary|std::ios::trunc);
    file.write(reinterpret_cast<const char*>(output.data()),output.size());
    file.close();
    if(!file)return L"No se pudo escribir el .bo1scene";
    std::wstring details=L"PAQUETE DE RECURSOS CREADO\n";
    details+=std::to_wstring(pack.meshes.size())+L" modelos; "+
             std::to_wstring(textureIds.size())+L" texturas; "+
             std::to_wstring(output.size()/1024)+L" KiB.\n";
    details+=L"Archivos FF examinados: "+std::to_wstring(attempted)+L".\n";
    details+=L"Texturas por nombre de material: "+std::to_wstring(nameMatches)+
             L"; asignaciones sin verificar: "+std::to_wstring(fallbackMatches)+L".\n";
    details+=L"IMPORTANTE: no es una escena original del mapa. Los modelos se\n";
    details+=L"muestran en posiciones de prueba; falta reconstruir GfxWorld.";
    return details;
}
void StartAutoExport() {
    const auto folder=ChooseGameFolder();
    if(folder.empty())return;
    auto save=Pick(true,L"Paquete para iPhone (*.bo1scene)\0*.bo1scene\0\0",L"bo1scene");
    if(save.empty())return;
    busy=true;UpdateLabels();
    SetWindowTextW(statusLabel,L"Analizando FastFiles y asociaciones de texturas en IWD...");
    std::thread([folder,save](){
        auto value=std::make_unique<std::wstring>(AutomaticExport(folder,save));
        PostMessageW(windowHandle,Finished,0,
                    reinterpret_cast<LPARAM>(value.release()));
    }).detach();
}

void StartExport() {
    const auto output=Pick(true,L"Escenas ligeras (*.bo1scene)\0*.bo1scene\0\0",L"bo1scene");
    if(output.empty())return;
    busy=true;UpdateLabels();
    SetWindowTextW(statusLabel,L"Validando FastFiles y creando escena ligera...");
    const auto chosen=files;
    const auto texture=imagePath;
    std::thread([chosen,texture,output](){
        auto result=std::make_unique<std::wstring>(Export(chosen,texture,output));
        PostMessageW(windowHandle,Finished,0,reinterpret_cast<LPARAM>(result.release()));
    }).detach();
}
LRESULT CALLBACK WindowProc(HWND hwnd,UINT msg,WPARAM w,LPARAM l) {
    if(msg==WM_CREATE) {
        const HINSTANCE module=GetModuleHandleW(nullptr);
        CreateWindowW(L"STATIC",L"BO1 Zombies • Preparador de escenas ligeras",
                      WS_CHILD|WS_VISIBLE,20,20,570,32,hwnd,nullptr,module,nullptr);
        autoButton=CreateWindowW(L"BUTTON",L"AUTOMATICO: ANALIZAR CARPETA DEL JUEGO",
                       WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON,20,60,540,46,hwnd,
                       reinterpret_cast<HMENU>(105),module,nullptr);
        modelsLabel=CreateWindowW(L"STATIC",L"Modelos .ff: 0 / 6",
                       WS_CHILD|WS_VISIBLE,20,122,530,28,hwnd,nullptr,module,nullptr);
        addModelButton=CreateWindowW(L"BUTTON",L"1. Añadir modelo .ff",
                       WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,20,156,265,42,hwnd,
                       reinterpret_cast<HMENU>(101),module,nullptr);
        clearButton=CreateWindowW(L"BUTTON",L"Limpiar modelos",
                       WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,295,156,265,42,hwnd,
                       reinterpret_cast<HMENU>(104),module,nullptr);
        imageButton=CreateWindowW(L"BUTTON",L"2. Elegir textura .iwi (DXT3)",
                       WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,20,210,540,42,hwnd,
                       reinterpret_cast<HMENU>(102),module,nullptr);
        saveButton=CreateWindowW(L"BUTTON",L"3. Exportar escena .bo1scene",
                       WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,20,264,540,42,hwnd,
                       reinterpret_cast<HMENU>(103),module,nullptr);
        statusLabel=CreateWindowW(L"STATIC",
                       L"Seleccioná AUTOMATICO para buscar recursos dentro del juego sin extraer .iwi.\nLos controles manuales son opcionales. Todavía no interpreta GfxWorld.",
                       WS_CHILD|WS_VISIBLE|SS_LEFT,20,320,550,120,hwnd,nullptr,module,nullptr);
        UpdateLabels();return 0;
    }
    if(msg==WM_COMMAND) {
        switch(LOWORD(w)) {
        case 105: if(!busy)StartAutoExport();return 0;
        case 101: {
            auto picked=Pick(false,L"FastFiles (*.ff)\0*.ff\0\0",L"ff");
            if(!picked.empty()&&files.size()<bo1::scene_pack::MaxModels) {
                files.push_back(std::move(picked));UpdateLabels();
                SetWindowTextW(statusLabel,FileName(files.back()).c_str());
            }
            return 0;
        }
        case 102: {
            auto picked=Pick(false,L"Texturas IWI (*.iwi)\0*.iwi\0\0",L"iwi");
            if(!picked.empty()){imagePath=std::move(picked);UpdateLabels();
                SetWindowTextW(statusLabel,FileName(imagePath).c_str());}
            return 0;
        }
        case 103: if(!busy) StartExport();return 0;
        case 104: files.clear();UpdateLabels();SetWindowTextW(statusLabel,L"Modelos borrados.");return 0;
        }return 0;
    }
    if(msg==Finished) {
        std::unique_ptr<std::wstring> result(reinterpret_cast<std::wstring*>(l));
        busy=false;UpdateLabels();SetWindowTextW(statusLabel,result->c_str());
        return 0;
    }
    if(msg==WM_DESTROY){windowHandle=nullptr;PostQuitMessage(0);return 0;}
    return DefWindowProcW(hwnd,msg,w,l);
}
}
int WINAPI wWinMain(HINSTANCE module,HINSTANCE,LPWSTR,int show) {
    const wchar_t* className=L"BO1ScenePackWindow";
    WNDCLASSW c{};
    c.lpfnWndProc=WindowProc;
    c.hInstance=module;
    c.lpszClassName=className;
    c.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
    c.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));
    if(!RegisterClassW(&c))return 1;
    windowHandle=CreateWindowW(className,L"BO1 iOS - Escena para iPhone",
        WS_OVERLAPPEDWINDOW&~WS_THICKFRAME&~WS_MAXIMIZEBOX,CW_USEDEFAULT,
        CW_USEDEFAULT,610,500,nullptr,nullptr,module,nullptr);
    if(!windowHandle)return 1;
    ShowWindow(windowHandle,show);UpdateWindow(windowHandle);
    MSG message;
    while(GetMessageW(&message,nullptr,0,0)>0) {
        TranslateMessage(&message);DispatchMessageW(&message);
    }
    return int(message.wParam);
}

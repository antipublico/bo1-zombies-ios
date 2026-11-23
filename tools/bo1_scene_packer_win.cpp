#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
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
#include <cstdio>

// Local Windows GUI; picks user-owned .ff and .iwi. Export contains ONLY
// decoded LOD0 CPU vertices/triangles and one BC2 top mip, not the source files.
namespace {
constexpr UINT Finished = WM_APP + 1;
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
        modelsLabel=CreateWindowW(L"STATIC",L"Modelos .ff: 0 / 6",
                       WS_CHILD|WS_VISIBLE,20,66,530,28,hwnd,nullptr,module,nullptr);
        addModelButton=CreateWindowW(L"BUTTON",L"1. Añadir modelo .ff",
                       WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,20,100,265,42,hwnd,
                       reinterpret_cast<HMENU>(101),module,nullptr);
        clearButton=CreateWindowW(L"BUTTON",L"Limpiar modelos",
                       WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,295,100,265,42,hwnd,
                       reinterpret_cast<HMENU>(104),module,nullptr);
        imageButton=CreateWindowW(L"BUTTON",L"2. Elegir textura .iwi (DXT3)",
                       WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,20,154,540,42,hwnd,
                       reinterpret_cast<HMENU>(102),module,nullptr);
        saveButton=CreateWindowW(L"BUTTON",L"3. Exportar escena .bo1scene",
                       WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON,20,208,540,42,hwnd,
                       reinterpret_cast<HMENU>(103),module,nullptr);
        statusLabel=CreateWindowW(L"STATIC",
                       L"Elegí un archivo .ff del juego y una textura .iwi.\nNo se suben archivos. El escenario es de prueba, no un mapa completo.",
                       WS_CHILD|WS_VISIBLE|SS_LEFT,20,263,550,80,hwnd,nullptr,module,nullptr);
        UpdateLabels();return 0;
    }
    if(msg==WM_COMMAND) {
        switch(LOWORD(w)) {
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
    c.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    if(!RegisterClassW(&c))return 1;
    windowHandle=CreateWindowW(className,L"BO1 iOS - Escena para iPhone",
        WS_OVERLAPPEDWINDOW&~WS_THICKFRAME&~WS_MAXIMIZEBOX,CW_USEDEFAULT,
        CW_USEDEFAULT,610,410,nullptr,nullptr,module,nullptr);
    if(!windowHandle)return 1;
    ShowWindow(windowHandle,show);UpdateWindow(windowHandle);
    MSG message;
    while(GetMessageW(&message,nullptr,0,0)>0) {
        TranslateMessage(&message);DispatchMessageW(&message);
    }
    return int(message.wParam);
}

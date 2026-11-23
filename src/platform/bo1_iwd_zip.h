#pragma once
#include <platform/bo1_bytecode_io.h>
#include <platform/bo1_iwi_image.h>
#include <zlib/zlib.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// Small bounded read-only ZIP/IWD indexer for assets in the user's locally
// installed game. No extraction paths and no external executable required.
// Supports ordinary ZIP store/deflate, refuses encrypted, zip64 or huge entries.
// It loads only one selected .iwi from the archive; never unpacks everything.
namespace bo1::iwd {
struct Entry {
    std::string name;
    std::uint32_t compressed = 0, original = 0, crc = 0, localOffset = 0;
    std::uint16_t flags = 0, method = 0;
};
inline constexpr std::uint32_t MaxEntryBytes = 12u * 1024 * 1024;
inline std::uint16_t U16(const std::uint8_t* p) {
    return bo1::bytecode::Load<std::uint16_t>(p);
}
inline std::uint32_t U32(const std::uint8_t* p) {
    return bo1::bytecode::Load<std::uint32_t>(p);
}
inline std::string Lower(std::string s) {
    std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){
        return static_cast<char>(std::tolower(c));
    });
    std::replace(s.begin(),s.end(),'\\','/');
    return s;
}
inline bool ReadAt(std::ifstream& f,std::uint64_t offset,
                   std::uint8_t* out,std::size_t n) {
    if(n>MaxEntryBytes || offset>static_cast<std::uint64_t>(INT64_MAX))
        return false;
    f.clear();
    f.seekg(static_cast<std::streamoff>(offset),std::ios::beg);
    return bool(f.read(reinterpret_cast<char*>(out),
                       static_cast<std::streamsize>(n)));
}
inline bool Index(const std::filesystem::path& archive,
                  std::vector<Entry>& entries,std::string& error) {
    entries.clear();error.clear();
    std::ifstream f(archive,std::ios::binary|std::ios::ate);
    if(!f){error="No se pudo abrir IWD";return false;}
    const auto end=f.tellg();
    if(end<std::streamoff(22)){
        error="Archivo IWD demasiado corto";return false;
    }
    const std::uint64_t size=static_cast<std::uint64_t>(end);
    const auto readBytes=static_cast<std::size_t>(std::min<std::uint64_t>(size,65557));
    std::vector<std::uint8_t> tail(readBytes);
    if(!ReadAt(f,size-readBytes,tail.data(),tail.size())){
        error="No se pudo leer directorio IWD";return false;
    }
    std::size_t tailEnd=tail.size();
    while(tailEnd>=22) {
        --tailEnd;
        if(U32(tail.data()+tailEnd)!=0x06054b50u)continue;
        if(tailEnd+22>tail.size())continue;
        // ZIP comment length must make this the final record.
        if(tailEnd+22+U16(tail.data()+tailEnd+20)!=tail.size())continue;
        const auto* e=tail.data()+tailEnd;
        const auto count=U16(e+10), bytes=U32(e+12), offset=U32(e+16);
        if(U16(e+4)!=0||U16(e+6)!=0||U16(e+8)!=count ||
           count==0xffff||bytes==0xffffffffu||offset==0xffffffffu||
           count>30000||std::uint64_t(offset)+bytes>size){
            error="IWD multi-volumen/Zip64 no admitido";return false;
        }
        if(bytes>64u*1024u*1024u) {
            error="Directorio IWD demasiado grande";return false;
        }
        std::vector<std::uint8_t> dir(bytes);
        if(!ReadAt(f,offset,dir.data(),bytes)){
            error="Directorio IWD truncado";return false;
        }
        std::size_t p=0;
        for(std::size_t k=0;k<count;++k) {
            if(dir.size()-p<46||U32(dir.data()+p)!=0x02014b50u) {
                error="Entrada ZIP inválida";entries.clear();return false;
            }
            auto* h=dir.data()+p;
            const std::size_t names=U16(h+28),extra=U16(h+30),comment=U16(h+32);
            const std::uint64_t advance=46ull+names+extra+comment;
            if(advance>dir.size()-p||names>1024) {
                error="Cabecera ZIP fuera de límites";entries.clear();return false;
            }
            Entry item{};
            item.flags=U16(h+8);item.method=U16(h+10);
            item.crc=U32(h+16);
            item.compressed=U32(h+20);
            item.original=U32(h+24);
            item.localOffset=U32(h+42);
            item.name=Lower(std::string(reinterpret_cast<char*>(h+46),names));
            // Skip directories, huge entries, encrypted, unsupported methods.
            if(!(item.flags&1u) && (item.method==0||item.method==8)
                && item.original>=48 && item.original<=MaxEntryBytes
                && item.compressed<=MaxEntryBytes &&
                item.name.size()>=4 &&
                item.name.substr(item.name.size()-4)==".iwi")
                entries.push_back(std::move(item));
            p+=static_cast<std::size_t>(advance);
        }
        return true;
    }
    error="No se encontró directorio ZIP dentro del IWD";
    return false;
}

inline bool Read(const std::filesystem::path& archive,const Entry& entry,
                 std::vector<std::uint8_t>& bytes,std::string& error) {
    bytes.clear();error.clear();
    if(entry.original>MaxEntryBytes||entry.compressed>MaxEntryBytes||
       (entry.flags&1u) || (entry.method!=0&&entry.method!=8)) {
        error="Entrada IWD no admitida";return false;
    }
    std::ifstream f(archive,std::ios::binary|std::ios::ate);
    if(!f){error="Archivo IWD no disponible";return false;}
    const auto end=f.tellg();
    if(end<std::streamoff(30)){error="IWD incompleto";return false;}
    const auto size=static_cast<std::uint64_t>(end);
    std::uint8_t h[30]{};
    if(entry.localOffset>size-30||
       !ReadAt(f,entry.localOffset,h,30)||U32(h)!=0x04034b50u) {
        error="Cabecera ZIP local inválida";return false;
    }
    const std::uint64_t start=std::uint64_t(entry.localOffset)+30+
        U16(h+26)+U16(h+28);
    if(start>size || entry.compressed>size-start) {
        error="Entrada IWI truncada";return false;
    }
    std::vector<std::uint8_t> compressed(entry.compressed);
    if(!ReadAt(f,start,compressed.data(),compressed.size())) {
        error="No se pudo leer IWI del IWD";return false;
    }
    std::vector<std::uint8_t> result(entry.original);
    if(entry.method==0) {
        if(entry.compressed!=entry.original){error="ZIP store incompleto";return false;}
        result=std::move(compressed);
    }else {
        z_stream zs{};
        zs.next_in=compressed.data();zs.avail_in=static_cast<uInt>(compressed.size());
        zs.next_out=result.data();zs.avail_out=static_cast<uInt>(result.size());
        if(inflateInit2(&zs,-MAX_WBITS)!=Z_OK) {
            error="No se pudo inicializar deflate";return false;
        }
        const int ret=inflate(&zs,Z_FINISH);
        const bool good=ret==Z_STREAM_END && zs.total_out==entry.original &&
                        zs.total_in==entry.compressed;
        inflateEnd(&zs);
        if(!good){error="Datos ZIP corruptos";return false;}
    }
    if(crc32(0,result.data(),static_cast<uInt>(result.size())) != entry.crc) {
        error="IWD CRC32 inválido";return false;
    }
    bo1::texture::IwiHeader header{};
    if(bo1::texture::ParseIwiHeader(result.data(),result.size(),result.size(),header)
          !=bo1::texture::IwiParseResult::Ok) {
        error="Imagen IWI con cabecera inválida";return false;
    }
    std::uint64_t off=0;std::size_t n=0;
    if(!bo1::texture::IwiBC2TopMipRange(header,off,n)){
        error="IWI sin DXT3 2D legible";return false;
    }
    bytes=std::move(result);
    return true;
}
} // namespace bo1::iwd

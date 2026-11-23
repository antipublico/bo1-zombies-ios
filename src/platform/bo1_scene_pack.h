#pragma once
#include <platform/bo1_model_mesh.h>
#include <platform/bo1_texture_bc.h>
#include <platform/bo1_bytecode_io.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// BO1SCN01: compact portable diagnostic scene, not a full BO1 game/map.
// LE fixed header (32 bytes), BC2 top-mip pixels, then bounded CPU mesh data.
// The original .ff/.iwd/.iwi files are never included in this package.
namespace bo1::scene_pack {
inline constexpr size_t MaxBytes = 16u * 1024 * 1024;
inline constexpr size_t MaxModels = 6;
inline constexpr size_t MaxVerticesPerModel = 65536;
inline constexpr size_t MaxIndicesPerModel = 196608;
inline constexpr char Magic[8] = {'B','O','1','S','C','N','0','1'};

struct Package {
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint8_t> bc2;
    std::vector<bo1::model::Mesh> meshes;
};

inline bool Validate(const Package& p, std::string& error) {
    std::size_t compressed=0, rgba=0;
    if (!bo1::texture::BC2ByteSizes(p.width,p.height,compressed,rgba) ||
        p.bc2.size()!=compressed || !p.width || !p.height) {
        error="BC2 texture dimensions/bytes invalid"; return false;
    }
    if (p.meshes.empty() || p.meshes.size()>MaxModels) {
        error="Expected between one and six models"; return false;
    }
    for (const auto& m:p.meshes) {
        if (m.name.size()>256 || m.vertices.empty() ||
            m.vertices.size()>MaxVerticesPerModel || m.indices.empty() ||
            m.indices.size()>MaxIndicesPerModel || m.indices.size()%3) {
            error="Mesh name, vertices or triangle count outside limits"; return false;
        }
        for (const auto& v:m.vertices) {
            if (!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z)||
                !std::isfinite(v.u)||!std::isfinite(v.v)||
                std::fabs(v.x)>1e6f||std::fabs(v.y)>1e6f||std::fabs(v.z)>1e6f) {
                error="Nonfinite or excessive vertex data"; return false;
            }
        }
        for (auto index:m.indices)
            if (index>=m.vertices.size()) {
                error="Triangle index outside mesh"; return false;
            }
    }
    error.clear();
    return true;
}

inline bool Encode(const Package& p, std::vector<std::uint8_t>& output,
                   std::string& error) {
    output.clear();
    if (!Validate(p,error)) return false;
    std::uint64_t needed=32+p.bc2.size();
    for (const auto& m:p.meshes)
        needed+=12+m.name.size()+std::uint64_t(m.vertices.size())*20+
                std::uint64_t(m.indices.size())*4;
    if (needed>MaxBytes) {error="Scene exceeds 16 MiB";return false;}
    output.reserve(static_cast<size_t>(needed));
    auto append32=[&](std::uint32_t n){
        std::uint8_t b[4];bo1::bytecode::Store<std::uint32_t>(b,n);
        output.insert(output.end(),b,b+4);
    };
    output.insert(output.end(),Magic,Magic+8);
    append32(1);append32(p.width);append32(p.height);
    append32(static_cast<std::uint32_t>(p.meshes.size()));
    append32(static_cast<std::uint32_t>(p.bc2.size()));append32(0);
    output.insert(output.end(),p.bc2.begin(),p.bc2.end());
    for (const auto& m:p.meshes) {
        append32(static_cast<std::uint32_t>(m.name.size()));
        append32(static_cast<std::uint32_t>(m.vertices.size()));
        append32(static_cast<std::uint32_t>(m.indices.size()));
        output.insert(output.end(),m.name.begin(),m.name.end());
        for(const auto& v:m.vertices) {
            for(float f:{v.x,v.y,v.z,v.u,v.v}){
                std::uint8_t b[4];bo1::bytecode::Store<float>(b,f);
                output.insert(output.end(),b,b+4);
            }
        }
        for(auto index:m.indices) append32(index);
    }
    if(output.size()!=needed){output.clear();error="Encoded size mismatch";return false;}
    return true;
}

inline bool Decode(const std::uint8_t* data, size_t size, Package& output,
                   std::string& error) {
    output={};
    if(!data||size<32||size>MaxBytes||
       std::memcmp(data,Magic,8)!=0) {
        error="Not a valid BO1SCN01 package";return false;
    }
    size_t pos=8;
    auto read32=[&](std::uint32_t& n)->bool {
        if(pos>size||size-pos<4)return false;
        n=bo1::bytecode::Load<std::uint32_t>(data+pos);pos+=4;return true;
    };
    std::uint32_t version=0,models=0,bc2size=0,reserved=0;
    Package p;
    if(!read32(version)||!read32(p.width)||!read32(p.height)||
       !read32(models)||!read32(bc2size)||!read32(reserved)||
       version!=1||reserved!=0||models==0||models>MaxModels) {
        error="Invalid scene header or version";return false;
    }
    std::size_t expected=0,rgba=0;
    if(!bo1::texture::BC2ByteSizes(p.width,p.height,expected,rgba)||
       expected!=bc2size||bc2size>size-pos) {
        error="Corrupt BC2 top mip";return false;
    }
    p.bc2.assign(data+pos,data+pos+bc2size);pos+=bc2size;
    for(std::uint32_t k=0;k<models;++k) {
        std::uint32_t name=0,vertices=0,indices=0;
        if(!read32(name)||!read32(vertices)||!read32(indices)||
           name>256||vertices==0||vertices>MaxVerticesPerModel||
           indices==0||indices>MaxIndicesPerModel||indices%3) {
            error="Invalid mesh header";return false;
        }
        const std::uint64_t count=std::uint64_t(name)+
            std::uint64_t(vertices)*20+std::uint64_t(indices)*4;
        if(count>size-pos){error="Truncated model body";return false;}
        bo1::model::Mesh mesh;
        mesh.name.assign(reinterpret_cast<const char*>(data+pos),name);pos+=name;
        mesh.vertices.reserve(vertices);
        bool first=true;
        for(std::uint32_t i=0;i<vertices;++i) {
            bo1::model::Vertex v{};
            v.x=bo1::bytecode::Load<float>(data+pos);
            v.y=bo1::bytecode::Load<float>(data+pos+4);
            v.z=bo1::bytecode::Load<float>(data+pos+8);
            v.u=bo1::bytecode::Load<float>(data+pos+12);
            v.v=bo1::bytecode::Load<float>(data+pos+16);
            pos+=20;
            for(int j=0;j<3;++j) {
                const float value=j==0?v.x:j==1?v.y:v.z;
                if(first){mesh.mins[j]=value;mesh.maxs[j]=value;}
                else {
                    mesh.mins[j]=std::min(mesh.mins[j],value);
                    mesh.maxs[j]=std::max(mesh.maxs[j],value);
                }
            }
            first=false;mesh.vertices.push_back(v);
        }
        mesh.indices.reserve(indices);
        for(std::uint32_t i=0;i<indices;++i) {
            std::uint32_t idx=0;
            if(!read32(idx)){error="Missing index";return false;}
            mesh.indices.push_back(idx);
        }
        mesh.surfaceCount=1; // topology is retained; per-surface materials are not.
        p.meshes.push_back(std::move(mesh));
    }
    if(pos!=size){error="Trailing bytes after last model";return false;}
    if(!Validate(p,error))return false;
    output=std::move(p);return true;
}
} // namespace bo1::scene_pack

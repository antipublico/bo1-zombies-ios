#include <platform/bo1_scene_pack.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <limits>
#include <vector>
#define CHECK(x) do { if(!(x)){std::fprintf(stderr,"scene pack test failed line %d: %s\n",__LINE__,#x);std::exit(1);} }while(false)
int main(){
    bo1::scene_pack::Package original;
    original.width=4;original.height=4;original.bc2.resize(16,0xff);
    bo1::model::Mesh mesh;
    mesh.name="synthetic crossed foliage";
    mesh.vertices={{-1,-1,0,0,1},{1,-1,0,1,1},{0,1,0,.5f,0}};
    mesh.indices={0,1,2};
    original.meshes.push_back(mesh);
    std::string error;
    std::vector<std::uint8_t> bytes;
    CHECK(bo1::scene_pack::Encode(original,bytes,error));
    CHECK(bytes.size()==32+16+12+mesh.name.size()+3*20+3*4);
    bo1::scene_pack::Package roundtrip;
    CHECK(bo1::scene_pack::Decode(bytes.data(),bytes.size(),roundtrip,error));
    CHECK(roundtrip.meshes.size()==1 && roundtrip.bc2==original.bc2);
    CHECK(roundtrip.meshes[0].name==mesh.name);
    CHECK(roundtrip.meshes[0].vertices[0].x==-1 && roundtrip.meshes[0].indices[2]==2);
    CHECK(roundtrip.meshes[0].mins[0]==-1 && roundtrip.meshes[0].maxs[1]==1);
    for(size_t n=0;n<bytes.size();++n){
        bo1::scene_pack::Package discarded;
        CHECK(!bo1::scene_pack::Decode(bytes.data(),n,discarded,error));
        CHECK(discarded.meshes.empty());
    }
    auto corrupt=bytes;corrupt[0]='X';
    CHECK(!bo1::scene_pack::Decode(corrupt.data(),corrupt.size(),roundtrip,error));
    corrupt=bytes;corrupt[12]=0xff;
    CHECK(!bo1::scene_pack::Decode(corrupt.data(),corrupt.size(),roundtrip,error));
    corrupt=bytes;corrupt.push_back(0);
    CHECK(!bo1::scene_pack::Decode(corrupt.data(),corrupt.size(),roundtrip,error));
    original.meshes[0].indices[2]=999;
    CHECK(!bo1::scene_pack::Encode(original,bytes,error));
    original.meshes[0].indices[2]=2;
    original.meshes[0].vertices[2].u=std::numeric_limits<float>::infinity();
    CHECK(!bo1::scene_pack::Encode(original,bytes,error));
    original.meshes[0].vertices[2].u=.5f;
    original.meshes[0].indices[2]=2;
    // V2: independently addressed textures for 2 model sources; v1 remains readable.
    original.meshes.push_back(mesh);
    bo1::scene_pack::Texture second{};
    second.width=4;second.height=4;second.bc2.resize(16,0x55);
    original.extraTextures.push_back(second);
    original.textureSlots={0,1};
    CHECK(bo1::scene_pack::Encode(original,bytes,error));
    CHECK(bytes[7]=='2');
    CHECK(bo1::scene_pack::Decode(bytes.data(),bytes.size(),roundtrip,error));
    CHECK(roundtrip.meshes.size()==2 && roundtrip.extraTextures.size()==1);
    CHECK(roundtrip.extraTextures[0].bc2==second.bc2);
    CHECK(roundtrip.textureSlots[0]==0 && roundtrip.textureSlots[1]==1);
    auto invalid=bytes;
    // Per-mesh slot is immediately after its 12-byte mesh header.
    const size_t secondTextureEnd=32+16+12+16;
    invalid[secondTextureEnd+12]=4;
    CHECK(!bo1::scene_pack::Decode(invalid.data(),invalid.size(),roundtrip,error));
    invalid=bytes;invalid[28]=7; // declares seven additional textures
    CHECK(!bo1::scene_pack::Decode(invalid.data(),invalid.size(),roundtrip,error));
    for(size_t i=0;i<bytes.size();++i) {
        bo1::scene_pack::Package discarded;
        CHECK(!bo1::scene_pack::Decode(bytes.data(),i,discarded,error));
    }
    original.textureSlots[1]=3;
    CHECK(!bo1::scene_pack::Encode(original,bytes,error));
    std::puts("BO1 scene pack roundtrip and malformed file tests passed");
}

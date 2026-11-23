#include <platform/bo1_iwd_zip.h>
#include <platform/bo1_bytecode_io.h>
#include <zlib/zlib.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>
#define CHECK(v) do { if(!(v)){std::fprintf(stderr,"IWD test failed line %d\n",__LINE__);std::exit(1);} } while(false)
static void put16(std::vector<std::uint8_t>& b,std::size_t i,std::uint16_t n){
    bo1::bytecode::Store<std::uint16_t>(b.data()+i,n);
}
static void put32(std::vector<std::uint8_t>& b,std::size_t i,std::uint32_t n){
    bo1::bytecode::Store<std::uint32_t>(b.data()+i,n);
}
int main(){
    const std::string name="images/demo.iwi";
    std::vector<std::uint8_t> iwi(64,0);
    iwi[0]='I';iwi[1]='W';iwi[2]='i';iwi[3]=13;iwi[4]=12;
    put16(iwi,6,4);put16(iwi,8,4);put16(iwi,10,1);
    put32(iwi,16,64);put32(iwi,20,48);
    const auto crc=crc32(0,iwi.data(),static_cast<uInt>(iwi.size()));
    // The Deflate bytes are an independently generated, raw RFC1951 stream
    // of the synthetic 64-byte IWI header + pixel fixture.
    const std::vector<std::uint8_t> compressed={
        0xf3,0x0c,0xcf,0xe4,0xe5,0x61,0x60,0x01,0x42,0x46,
        0x06,0x10,0x70,0x00,0x62,0x03,0x06,0xe2,0x01,0x00
    };
    for(unsigned compressionMethod : {0u,8u}){
    const auto& payload=compressionMethod==0?iwi:compressed;
    std::vector<std::uint8_t> zip(30+name.size()+payload.size(),0);
    put32(zip,0,0x04034b50);put16(zip,4,20);
    put16(zip,8,static_cast<std::uint16_t>(compressionMethod));put32(zip,14,crc);
    put32(zip,18,static_cast<std::uint32_t>(payload.size()));
    put32(zip,22,64);put16(zip,26,static_cast<std::uint16_t>(name.size()));
    std::copy(name.begin(),name.end(),zip.begin()+30);
    std::copy(payload.begin(),payload.end(),zip.begin()+30+name.size());
    const std::size_t central=zip.size();
    zip.resize(central+46+name.size(),0);
    put32(zip,central,0x02014b50);put16(zip,central+4,20);put16(zip,central+6,20);
    put16(zip,central+10,static_cast<std::uint16_t>(compressionMethod));
    put32(zip,central+16,crc);
    put32(zip,central+20,static_cast<std::uint32_t>(payload.size()));put32(zip,central+24,64);
    put16(zip,central+28,static_cast<std::uint16_t>(name.size()));
    std::copy(name.begin(),name.end(),zip.begin()+central+46);
    const std::size_t end=zip.size();
    zip.resize(end+22,0);
    put32(zip,end,0x06054b50);
    put16(zip,end+8,1);put16(zip,end+10,1);
    put32(zip,end+12,static_cast<std::uint32_t>(end-central));
    put32(zip,end+16,static_cast<std::uint32_t>(central));
    const auto path=std::filesystem::temp_directory_path()/"bo1-iwd-unit-test.iwd";
    {
        std::ofstream file(path,std::ios::binary|std::ios::trunc);
        file.write(reinterpret_cast<const char*>(zip.data()),zip.size());
        CHECK(bool(file));
    }
    std::string error;
    std::vector<bo1::iwd::Entry> entries;
    CHECK(bo1::iwd::Index(path,entries,error));
    CHECK(entries.size()==1&&entries[0].name==name);
    std::vector<std::uint8_t> decoded;
    CHECK(bo1::iwd::Read(path,entries[0],decoded,error));
    CHECK(decoded==iwi);
    entries[0].crc=0;
    CHECK(!bo1::iwd::Read(path,entries[0],decoded,error));
    std::filesystem::remove(path);
    }
    std::puts("IWD stored and Deflate textures extracted with CRC verified");
}

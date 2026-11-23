#pragma once
#include <algorithm>
#include <cctype>
#include <string>

// Exact file-to-material image linkage. Never use substring similarity:
// grass_piece1.iwi must not become a zombie's albedo by coincidence.
namespace bo1::texture_match {
inline std::string Canonical(std::string name) {
    std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){
        return static_cast<char>(std::tolower(c));
    });
    std::replace(name.begin(),name.end(),'\\','/');
    if(name.rfind("images/",0)==0)name.erase(0,7);
    if(name.size()>=4 && name.substr(name.size()-4)==".iwi")
        name.resize(name.size()-4);
    return name;
}
inline bool ExactImage(const std::string& materialImage,
                       const std::string& archiveEntry) {
    return !materialImage.empty() &&
        Canonical(materialImage)==Canonical(archiveEntry);
}
inline bool ZombieModel(const std::string& name) {
    std::string lower=Canonical(name);
    return lower.find("zombie")!=std::string::npos ||
           lower.find("undead")!=std::string::npos ||
           lower.find("walker")!=std::string::npos ||
           lower.find("zom_")!=std::string::npos;
}
} // namespace bo1::texture_match

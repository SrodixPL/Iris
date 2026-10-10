#include "core/features/player/models.hpp"
#include <Windows.h>
#include <array>
#include <cstring>
#include <fstream>
#include <algorithm>
namespace iris::models {
std::filesystem::path game_content() {
  wchar_t path[32768]{};
  if(const auto client=GetModuleHandleW(L"client.dll"))
    if(GetModuleFileNameW(client,path,32768))
      return std::filesystem::path(path).parent_path().parent_path().parent_path();
  return L"C:/Program Files (x86)/Steam/steamapps/common/Counter-Strike Global Offensive/game/csgo";
}
bool valid_resource_path(const std::string& resource) {
  if(resource.size()>240 || !resource.starts_with("models/") || !resource.ends_with(".vmdl"))return false;
  if(resource.find('\\')!=std::string::npos || resource.find(':')!=std::string::npos)return false;
  for(const auto& part:std::filesystem::path(resource))if(part==".." || part==".")return false;
  return true;
}
bool same_resource_path(std::string_view first,std::string_view second) {
  if(first.size()!=second.size())return false;
  const auto lower=[](unsigned char c){return c>='A' && c<='Z' ? c+('a'-'A') : c;};
  // ResourceSystem canonicalizes ASCII path case when resolving models.
  return std::equal(first.begin(),first.end(),second.begin(),[&](char a,char b){return lower(a)==lower(b);});
}
bool compiled_model(const std::filesystem::path& file) {
  std::error_code ec;const auto length=std::filesystem::file_size(file,ec);
  if(ec || length<28 || length>256*1024*1024)return false;
  std::ifstream input(file,std::ios::binary);std::array<unsigned char,16> header{};
  if(!input.read(reinterpret_cast<char*>(header.data()),header.size()))return false;
  const auto u32=[&](int at){std::uint32_t n;std::memcpy(&n,header.data()+at,4);return n;};
  std::uint16_t version;std::memcpy(&version,header.data()+4,2);
  const auto count=u32(12);const auto table=std::uint64_t{8}+u32(8);
  if(version!=12 || u32(0)!=length || !count || count>128 || table+count*12>length)return false;
  bool data=false;
  input.seekg(static_cast<std::streamoff>(table));
  for(unsigned i=0;i<count;++i){
    std::array<char,12> block{};if(!input.read(block.data(),block.size()))return false;
    std::uint32_t offset,size;std::memcpy(&offset,block.data()+4,4);std::memcpy(&size,block.data()+8,4);
    const auto start=table+i*12+4+offset;
    if(start>length || size>length-start)return false;
    data |= std::memcmp(block.data(),"DATA",4)==0 && size>0;
  }
  return data;
}
Scan scan(const std::filesystem::path& content) {
  Scan result;const auto root=content/"models";
  try {
    if(!std::filesystem::exists(root)){result.detail="Place compiled .vmdl_c models in "+root.string();return result;}
    unsigned examined=0,rejected=0;
    for(const auto& entry:std::filesystem::recursive_directory_iterator(root,std::filesystem::directory_options::skip_permission_denied)) {
      if(++examined>20000){result.detail="Scan limit reached; use a smaller model folder.";break;}
      if(!entry.is_regular_file() || entry.path().extension()!=".vmdl_c")continue;
      const auto canonical=std::filesystem::weakly_canonical(entry.path());
      const auto relative=canonical.lexically_relative(std::filesystem::weakly_canonical(content));
      auto resource=relative.generic_string();resource.resize(resource.size()-2);
      for(char& c:resource)if(c>='A' && c<='Z')c+=('a'-'A');
      if(!valid_resource_path(resource) || !compiled_model(canonical)){++rejected;continue;}
      result.entries.push_back({relative.stem().stem().string(),resource});
      if(result.entries.size()>=512)break;
    }
    std::sort(result.entries.begin(),result.entries.end(),[](const auto& a,const auto& b){return a.resource<b.resource;});
    if(result.detail.empty())result.detail=std::to_string(result.entries.size())+" compiled models found; "+std::to_string(rejected)+" invalid resources skipped.";
  }catch(const std::exception& e){result.detail=e.what();}
  return result;
}
}

#pragma once
#include <Windows.h>
#include <bcrypt.h>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#pragma comment(lib,"bcrypt.lib")
namespace iris::sdk {
inline std::string hash_file(const std::filesystem::path& path) {
    std::ifstream stream(path,std::ios::binary);
    if(!stream)throw std::runtime_error("Cannot open CS2 binary for fingerprint verification.");
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)
        throw std::runtime_error("SHA256 provider unavailable.");
    bool okay=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
    std::array<char,65536> buffer{};
    while(okay && stream){
        stream.read(buffer.data(),buffer.size());
        if(stream.gcount())okay=BCryptHashData(hash,reinterpret_cast<PUCHAR>(buffer.data()),static_cast<ULONG>(stream.gcount()),0)>=0;
    }
    std::array<unsigned char,32> digest{};
    okay=okay && !stream.bad() && BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)>=0;
    if(hash)BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm,0);
    if(!okay)throw std::runtime_error("CS2 fingerprint verification failed.");
    char result[65]{};
    for(std::size_t i=0;i<digest.size();++i)std::snprintf(result+i*2,3,"%02x",digest[i]);
    return result;
}
}

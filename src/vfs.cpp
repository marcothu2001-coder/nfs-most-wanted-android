#include "ea_chunks.h"
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <vector>
#include <cstring>

class NFSVirtualFileSystem {
private:
    std::string m_baseDir;
    std::unordered_map<u32, ZDirRecord> m_catalog;

public:
    bool Mount(const std::string& zdirFilePath, const std::string& dataDirectory) {
        m_baseDir = dataDirectory;
        if (!m_baseDir.empty() && m_baseDir.back() != '/' && m_baseDir.back() != '\\') {
            m_baseDir += '/';
        }

        std::ifstream file(zdirFilePath, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "[VFS] Erro: Falha ao abrir o arquivo de catalogo: " << zdirFilePath << "\n";
            return false;
        }

        file.seekg(0, std::ios::end);
        size_t fileSize = file.tellg();
        file.seekg(0, std::ios::beg);

        size_t recordCount = fileSize / sizeof(ZDirRecord);
        std::vector<ZDirRecord> records(recordCount);
        file.read(reinterpret_cast<char*>(records.data()), fileSize);

        m_catalog.clear();
        for (const auto& rec : records) {
            m_catalog[rec.primary_hash] = rec;
        }

        std::cout << "[VFS] Catalogo ZDIR montado com sucesso. " << m_catalog.size() 
                  << " arquivos virtuais disponiveis.\n";
        return true;
    }

    bool HasFile(const std::string& path) const {
        u32 h = bStringHash(path);
        return m_catalog.find(h) != m_catalog.end();
    }

    std::vector<u8> ReadFile(const std::string& path) {
        u32 h = bStringHash(path);
        auto it = m_catalog.find(h);
        if (it == m_catalog.end()) {
            std::cerr << "[VFS] Arquivo nao encontrado no catalogo: " << path << " (Hash: 0x" 
                      << std::hex << h << std::dec << ")\n";
            return {};
        }

        const auto& rec = it->second;
        std::string archivePath = m_baseDir + "ZZDATA" + std::to_string(rec.archive_idx) + ".BIN";

        std::ifstream archive(archivePath, std::ios::binary);
        if (!archive.is_open()) {
            std::cerr << "[VFS] Erro ao abrir fatia de arquivo: " << archivePath << "\n";
            return {};
        }

        u64 byteOffset = static_cast<u64>(rec.sector_offset) * 2048ULL;
        archive.seekg(byteOffset, std::ios::beg);

        // Se for arquivo em chunk da EA, lemos o tamanho declarado
        u32 readSize = rec.uncompressed_sz > 0 ? rec.uncompressed_sz : rec.compressed_sz;
        std::vector<u8> buffer(readSize);
        archive.read(reinterpret_cast<char*>(buffer.data()), readSize);

        return buffer;
    }

    const ZDirRecord* GetRecord(const std::string& path) const {
        u32 h = bStringHash(path);
        auto it = m_catalog.find(h);
        return (it != m_catalog.end()) ? &it->second : nullptr;
    }
};

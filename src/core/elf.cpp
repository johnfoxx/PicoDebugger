#include "picodbg/elf.hpp"
#include <algorithm>
#include <fstream>
#include <iterator>
#include <limits>

namespace picodbg {
namespace {
class Reader {
public:
    const std::vector<uint8_t>& data;
    explicit Reader(const std::vector<uint8_t>& bytes) : data(bytes) {}
    void range(uint64_t offset, uint64_t size) const {
        if (offset > data.size() || size > data.size() - offset)
            throw Error(ErrorKind::InvalidImage, "ELF tronqué / offset hors fichier");
    }
    uint32_t number(uint64_t offset, unsigned size) const {
        range(offset, size);
        uint32_t value = 0;
        for (unsigned i = 0; i < size; ++i) value |= uint32_t(data[static_cast<size_t>(offset + i)]) << (8u * i);
        return value;
    }
    std::string string(uint64_t start, uint64_t size, uint32_t offset) const {
        range(start, size);
        if (offset >= size) throw Error(ErrorKind::InvalidImage, "Index de chaîne ELF invalide");
        const auto first = data.begin() + static_cast<ptrdiff_t>(start + offset);
        const auto end = data.begin() + static_cast<ptrdiff_t>(start + size);
        const auto zero = std::find(first, end, uint8_t(0));
        if (zero == end) throw Error(ErrorKind::InvalidImage, "Chaîne ELF non terminée");
        return std::string(first, zero);
    }
};
struct Section { uint32_t name, type, address, offset, size, link, entrySize; };
}
ElfImage ElfImage::read(const std::string& path) {
    std::ifstream input(path.c_str(), std::ios::binary | std::ios::ate);
    if (!input) throw Error(ErrorKind::InvalidImage, "Impossible d'ouvrir " + path);
    const auto length = input.tellg();
    if (length < 0 || length > 64 * 1024 * 1024) throw Error(ErrorKind::InvalidImage, "ELF trop volumineux (maximum 64 Mio)");
    input.seekg(0);
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    if (!bytes.empty() && !input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        throw Error(ErrorKind::InvalidImage, "Lecture ELF incomplète");
    return parse(bytes);
}
ElfImage ElfImage::parse(const std::vector<uint8_t>& bytes) {
    if (bytes.size() > 64u * 1024u * 1024u) throw Error(ErrorKind::InvalidImage, "ELF trop volumineux (maximum 64 Mio)");
    Reader r(bytes);
    r.range(0, 52);
    if (r.number(0, 4) != 0x464c457fu || bytes[4] != 1 || bytes[5] != 1 || bytes[6] != 1)
        throw Error(ErrorKind::InvalidImage, "ELF32 little-endian requis");
    if (r.number(16, 2) != 2 || r.number(18, 2) != 40 || r.number(20, 4) != 1)
        throw Error(ErrorKind::InvalidImage, "Exécutable ELF ARM lié (ET_EXEC) requis");
    if (r.number(40, 2) != 52) throw Error(ErrorKind::InvalidImage, "Taille d'en-tête ELF incorrecte");
    ElfImage image;
    image.entry = r.number(24, 4);
    const uint32_t phoff = r.number(28, 4), shoff = r.number(32, 4);
    const uint32_t phsize = r.number(42, 2), phcount = r.number(44, 2);
    const uint32_t shsize = r.number(46, 2), shcount = r.number(48, 2), shnames = r.number(50, 2);
    if (!phcount || phsize != 32 || (shcount && shsize != 40))
        throw Error(ErrorKind::InvalidImage, "Tables ELF absentes ou non prises en charge");
    r.range(phoff, uint64_t(phsize) * phcount);
    for (uint32_t i = 0; i < phcount; ++i) {
        const uint64_t p = uint64_t(phoff) + i * phsize;
        const uint32_t type = r.number(p, 4);
        if (type == 2 || type == 3) throw Error(ErrorKind::InvalidImage, "ELF dynamique non pris en charge");
        if (type != 1) continue;
        const auto offset = r.number(p + 4, 4), vaddr = r.number(p + 8, 4), paddr = r.number(p + 12, 4);
        const auto filesz = r.number(p + 16, 4), memsz = r.number(p + 20, 4);
        if (filesz > memsz || memsz > 16 * 1024 * 1024 || uint64_t(vaddr) + memsz > 0x100000000ull)
            throw Error(ErrorKind::InvalidImage, "Taille de segment ELF invalide");
        r.range(offset, filesz);
        if (filesz == 0 && memsz == 0) continue;
        // p_paddr is the load address. For .data in Flash with a RAM VMA, the
        // firmware startup performs the copy; pre-copying would hide startup bugs.
        Segment segment;
        segment.address = paddr;
        segment.bytes.assign(bytes.begin() + offset, bytes.begin() + offset + filesz);
        if (vaddr == paddr) segment.bytes.resize(memsz, 0);
        image.segments.push_back(std::move(segment));
    }
    if (image.segments.empty()) throw Error(ErrorKind::InvalidImage, "ELF sans segment chargeable");
    r.range(shoff, uint64_t(shsize) * shcount);
    std::vector<Section> sections;
    for (uint32_t i = 0; i < shcount; ++i) {
        const uint64_t p = uint64_t(shoff) + i * shsize;
        sections.push_back({r.number(p, 4), r.number(p + 4, 4), r.number(p + 12, 4),
            r.number(p + 16, 4), r.number(p + 20, 4), r.number(p + 24, 4), r.number(p + 36, 4)});
    }
    if (shcount && shnames >= shcount) throw Error(ErrorKind::InvalidImage, "Table des noms ELF invalide");
    for (const auto& section : sections) {
        if (shnames) {
            const auto& names = sections[shnames];
            const auto name = r.string(names.offset, names.size, section.name);
            if (name == ".vectors" || name == ".isr_vector" || name == ".vector_table") {
                image.vectorAddress = section.address; image.hasVectors = true;
            }
        }
        if (section.type != 2 && section.type != 11) continue;
        if (section.entrySize != 16 || section.size % 16 || section.link >= sections.size())
            throw Error(ErrorKind::InvalidImage, "Table de symboles ELF invalide");
        r.range(section.offset, section.size);
        const auto& names = sections[section.link];
        if (names.type != 3) throw Error(ErrorKind::InvalidImage, "Table de chaînes ELF invalide");
        for (uint32_t i = 0; i < section.size; i += 16) {
            const uint64_t p = uint64_t(section.offset) + i;
            if (r.number(p + 14, 2) == 0) continue; // Undefined symbol.
            const auto name = r.string(names.offset, names.size, r.number(p, 4));
            const auto value = r.number(p + 4, 4);
            if (name.empty() || name[0] == '$') continue;
            image.symbols[name] = value;
            if ((r.number(p + 12, 1) & 15u) == 2) {
                image.functions[value & ~1u] = name;
                image.functionSizes[value & ~1u] = r.number(p + 8, 4);
            }
            if (name == "__vectors" || name == "__VECTOR_TABLE") {
                image.vectorAddress = value; image.hasVectors = true;
            }
        }
    }
    return image;
}
void ElfImage::install(Memory& memory) const {
    for (const auto& segment : segments) memory.load(segment.address, segment.bytes);
}
std::string ElfImage::functionAt(uint32_t address) const {
    auto it = functions.upper_bound(address);
    if (it == functions.begin()) return {};
    --it;
    if (address == it->first) return it->second;
    const auto size = functionSizes.find(it->first);
    if (size != functionSizes.end() && size->second)
        return address - it->first < size->second ? it->second : std::string();
    for (const auto& segment : segments)
        if (it->first >= segment.address && address - segment.address < segment.bytes.size()) return it->second;
    return {};
}
std::string ElfImage::symbolAt(uint32_t address) const {
    auto it = functions.upper_bound(address);
    if (it == functions.begin()) return {};
    --it;
    return it->second + "+" + hex(address - it->first, 1);
}
}

#include "export/ExeResources.h"

#include <motor/core/Paths.h>
#include <motor/project/Project.h>

#include <algorithm>
#include <format>
#include <fstream>
#include <iterator>
#include <string_view>

#ifdef _WIN32
#include <windows.h>
#endif

namespace editor::ExeResources {

namespace fs = std::filesystem;

namespace {

constexpr uint32_t kIconDirSize = 6;
constexpr uint32_t kIconEntrySize = 16;    // ICONDIRENTRY no arquivo
constexpr uint32_t kGroupEntrySize = 14;   // GRPICONDIRENTRY no recurso
constexpr uint16_t kLanguage = 0x0416;     // pt-BR
constexpr uint16_t kCodePage = 1200;       // Unicode
constexpr const wchar_t* kIconGroupName = L"GLFW_ICON";

uint16_t U16(const std::vector<uint8_t>& b, size_t at) { return static_cast<uint16_t>(b[at] | (b[at + 1] << 8)); }
uint32_t U32(const std::vector<uint8_t>& b, size_t at) { return U16(b, at) | (static_cast<uint32_t>(U16(b, at + 2)) << 16); }
uint32_t U32BigEndian(const std::vector<uint8_t>& b, size_t at) {
    return (static_cast<uint32_t>(b[at]) << 24) | (b[at + 1] << 16) | (b[at + 2] << 8) | b[at + 3];
}

void Put16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>(v >> 8));
}
void Put32(std::vector<uint8_t>& out, uint32_t v) {
    Put16(out, static_cast<uint16_t>(v & 0xFFFF));
    Put16(out, static_cast<uint16_t>(v >> 16));
}
void Pad4(std::vector<uint8_t>& out) {
    while (out.size() % 4 != 0) out.push_back(0);
}
void PutText(std::vector<uint8_t>& out, std::u16string_view text) {
    for (char16_t c : text) Put16(out, static_cast<uint16_t>(c));
    Put16(out, 0);
}

bool IsPng(const std::vector<uint8_t>& b) {
    static constexpr uint8_t kSignature[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    return b.size() >= 24 && std::equal(std::begin(kSignature), std::end(kSignature), b.begin());
}

// UTF-8 -> UTF-16 (o texto dos recursos de versão).
std::u16string ToUtf16(std::string_view text) {
    const std::u16string wide = motor::PathFromUtf8(text).u16string();
    return wide;
}

// Bloco da árvore do VS_VERSIONINFO: cabeçalho, chave, valor e filhos, cada parte alinhada em 4 bytes.
// wLength não conta o enchimento depois do bloco.
std::vector<uint8_t> Block(std::u16string_view key, const std::vector<uint8_t>& value, uint16_t valueLength,
                           uint16_t type, const std::vector<std::vector<uint8_t>>& children) {
    std::vector<uint8_t> out;
    Put16(out, 0);  // wLength, preenchido no fim
    Put16(out, valueLength);
    Put16(out, type);
    PutText(out, key);
    Pad4(out);
    out.insert(out.end(), value.begin(), value.end());
    for (const auto& child : children) {
        Pad4(out);
        out.insert(out.end(), child.begin(), child.end());
    }
    out[0] = static_cast<uint8_t>(out.size() & 0xFF);
    out[1] = static_cast<uint8_t>(out.size() >> 8);
    return out;
}

std::vector<uint8_t> StringBlock(std::u16string_view key, std::string_view value) {
    std::vector<uint8_t> text;
    const std::u16string wide = ToUtf16(value);
    PutText(text, wide);
    return Block(key, text, static_cast<uint16_t>(wide.size() + 1), 1, {});  // tamanho em caracteres, com o 0
}

} // namespace

bool ReadIcon(const fs::path& file, std::vector<IconImage>& images, std::string& error) {
    images.clear();
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        error = std::format("não foi possível abrir o ícone '{}'", motor::ToUtf8(file));
        return false;
    }
    const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};

    if (IsPng(bytes)) {
        IconImage image;
        image.width = static_cast<int>(U32BigEndian(bytes, 16));  // IHDR
        image.height = static_cast<int>(U32BigEndian(bytes, 20));
        if (image.width < 1 || image.height < 1 || image.width > 256 || image.height > 256) {
            error = std::format("o ícone PNG tem {}x{}; use até 256x256 (o ideal é 256x256)", image.width, image.height);
            return false;
        }
        image.data = bytes;
        images.push_back(std::move(image));
        return true;
    }

    if (bytes.size() < kIconDirSize || U16(bytes, 0) != 0 || U16(bytes, 2) != 1 || U16(bytes, 4) == 0) {
        error = "o ícone precisa ser um .ico ou um .png";
        return false;
    }
    const uint16_t count = U16(bytes, 4);
    if (bytes.size() < kIconDirSize + count * kIconEntrySize) {
        error = "o .ico está cortado";
        return false;
    }
    for (uint16_t i = 0; i < count; ++i) {
        const size_t entry = kIconDirSize + i * kIconEntrySize;
        const uint32_t size = U32(bytes, entry + 8);
        const uint32_t offset = U32(bytes, entry + 12);
        if (size == 0 || offset > bytes.size() || size > bytes.size() - offset) {
            error = "o .ico está cortado";
            return false;
        }
        IconImage image;
        image.width = bytes[entry] == 0 ? 256 : bytes[entry];
        image.height = bytes[entry + 1] == 0 ? 256 : bytes[entry + 1];
        image.bitCount = U16(bytes, entry + 6);
        image.data.assign(bytes.begin() + offset, bytes.begin() + offset + size);
        if (image.bitCount == 0 && !IsPng(image.data) && image.data.size() >= 16) image.bitCount = U16(image.data, 14);
        images.push_back(std::move(image));
    }
    return true;
}

std::vector<uint8_t> BuildVersionResource(const VersionInfo& info) {
    int v[4] = {};
    motor::ParseGameVersion(info.version, v);
    std::vector<uint8_t> fixed;  // VS_FIXEDFILEINFO
    Put32(fixed, 0xFEEF04BD);
    Put32(fixed, 0x00010000);
    for (int twice = 0; twice < 2; ++twice) {  // versão do arquivo e do produto
        Put32(fixed, (static_cast<uint32_t>(v[0]) << 16) | static_cast<uint32_t>(v[1]));
        Put32(fixed, (static_cast<uint32_t>(v[2]) << 16) | static_cast<uint32_t>(v[3]));
    }
    Put32(fixed, 0x3F);     // dwFileFlagsMask
    Put32(fixed, 0);        // dwFileFlags
    Put32(fixed, 0x40004);  // VOS_NT_WINDOWS32
    Put32(fixed, 1);        // VFT_APP
    Put32(fixed, 0);
    Put32(fixed, 0);
    Put32(fixed, 0);

    const std::vector<uint8_t> table = Block(u"041604B0", {}, 0, 1,
                                             {StringBlock(u"FileDescription", info.name),
                                              StringBlock(u"FileVersion", info.version),
                                              StringBlock(u"InternalName", info.name),
                                              StringBlock(u"OriginalFilename", info.executable),
                                              StringBlock(u"ProductName", info.name),
                                              StringBlock(u"ProductVersion", info.version)});
    std::vector<uint8_t> translation;
    Put16(translation, kLanguage);
    Put16(translation, kCodePage);
    const std::vector<uint8_t> var = Block(u"Translation", translation, 4, 0, {});
    return Block(u"VS_VERSION_INFO", fixed, static_cast<uint16_t>(fixed.size()), 0,
                 {Block(u"StringFileInfo", {}, 0, 1, {table}), Block(u"VarFileInfo", {}, 0, 1, {var})});
}

bool Apply(const fs::path& exe, const std::vector<IconImage>& icon, const VersionInfo& info, std::string& error) {
#ifdef _WIN32
    HANDLE update = BeginUpdateResourceW(exe.c_str(), FALSE);
    if (!update) {
        error = std::format("não foi possível abrir '{}' para gravar o ícone e a versão (erro {})",
                            motor::ToUtf8(exe), GetLastError());
        return false;
    }
    bool ok = true;
    const WORD language = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);
    if (!icon.empty()) {
        std::vector<uint8_t> group;  // GRPICONDIR
        Put16(group, 0);
        Put16(group, 1);
        Put16(group, static_cast<uint16_t>(icon.size()));
        for (size_t i = 0; i < icon.size() && ok; ++i) {
            const IconImage& image = icon[i];
            const WORD id = static_cast<WORD>(i + 1);
            ok = UpdateResourceW(update, MAKEINTRESOURCEW(3) /* RT_ICON */, MAKEINTRESOURCEW(id), language,
                                 const_cast<uint8_t*>(image.data.data()), static_cast<DWORD>(image.data.size()));
            group.push_back(static_cast<uint8_t>(image.width >= 256 ? 0 : image.width));
            group.push_back(static_cast<uint8_t>(image.height >= 256 ? 0 : image.height));
            group.push_back(0);  // cores (0 = 256 ou mais)
            group.push_back(0);
            Put16(group, 1);  // planos
            Put16(group, image.bitCount);
            Put32(group, static_cast<uint32_t>(image.data.size()));
            Put16(group, id);
        }
        static_assert(kGroupEntrySize == 14);
        ok = ok && UpdateResourceW(update, MAKEINTRESOURCEW(14) /* RT_GROUP_ICON */, kIconGroupName, language,
                                   group.data(), static_cast<DWORD>(group.size()));
    }
    std::vector<uint8_t> version = BuildVersionResource(info);
    ok = ok && UpdateResourceW(update, MAKEINTRESOURCEW(16) /* RT_VERSION */, MAKEINTRESOURCEW(1), language,
                               version.data(), static_cast<DWORD>(version.size()));
    const DWORD failure = ok ? 0 : GetLastError();
    if (!EndUpdateResourceW(update, ok ? FALSE : TRUE) || !ok) {
        error = std::format("não foi possível gravar o ícone e a versão em '{}' (erro {})", motor::ToUtf8(exe),
                            ok ? GetLastError() : failure);
        return false;
    }
    return true;
#else
    (void)exe;
    (void)icon;
    (void)info;
    error = "ícone e versão no executável só no Windows";
    return false;
#endif
}

} // namespace editor::ExeResources

#include "ImageEncode.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>

namespace editor::mcp {

namespace {
uint32_t Crc32(const uint8_t* data, size_t size, uint32_t crc = 0) {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (size_t i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

void PutU32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(uint8_t(v >> 24));
    out.push_back(uint8_t(v >> 16));
    out.push_back(uint8_t(v >> 8));
    out.push_back(uint8_t(v));
}

void Chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    PutU32(out, static_cast<uint32_t>(data.size()));
    const size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    PutU32(out, Crc32(out.data() + start, out.size() - start));
}

// Escrita de bits na ordem do deflate (do bit menos significativo para o mais).
class BitWriter {
public:
    explicit BitWriter(std::vector<uint8_t>& out) : m_out(out) {}
    void Bits(uint32_t value, int count) {
        m_buffer |= uint64_t(value) << m_count;
        m_count += count;
        while (m_count >= 8) {
            m_out.push_back(uint8_t(m_buffer));
            m_buffer >>= 8;
            m_count -= 8;
        }
    }
    // Códigos de Huffman vão do bit mais significativo para o menos: inverte antes.
    void Code(uint32_t code, int length) {
        uint32_t reversed = 0;
        for (int i = 0; i < length; ++i) reversed |= ((code >> i) & 1u) << (length - 1 - i);
        Bits(reversed, length);
    }
    void Flush() {
        if (m_count > 0) m_out.push_back(uint8_t(m_buffer));
        m_buffer = 0;
        m_count = 0;
    }

private:
    std::vector<uint8_t>& m_out;
    uint64_t m_buffer = 0;
    int m_count = 0;
};

// Símbolo de literal/comprimento no código de Huffman fixo do deflate (RFC 1951, 3.2.6).
void FixedLiteral(BitWriter& w, int symbol) {
    if (symbol < 144) {
        w.Code(0x30 + symbol, 8);
    } else if (symbol < 256) {
        w.Code(0x190 + symbol - 144, 9);
    } else if (symbol < 280) {
        w.Code(symbol - 256, 7);
    } else {
        w.Code(0xC0 + symbol - 280, 8);
    }
}

constexpr int kLengthBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr int kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr int kDistBase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                               193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr int kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

void Match(BitWriter& w, int length, int distance) {
    int l = 28;
    while (kLengthBase[l] > length) --l;
    FixedLiteral(w, 257 + l);
    w.Bits(uint32_t(length - kLengthBase[l]), kLengthExtra[l]);
    int d = 29;
    while (kDistBase[d] > distance) --d;
    w.Code(uint32_t(d), 5);
    w.Bits(uint32_t(distance - kDistBase[d]), kDistExtra[d]);
}

// zlib com um bloco deflate de Huffman fixo e LZ77 guloso (cadeia de hash curta): bem menor que sem
// compressão para imagens de cena (céu, chão e faces lisas se repetem) e rápido o bastante para uma captura.
std::vector<uint8_t> Zlib(const std::vector<uint8_t>& raw) {
    constexpr int kWindow = 32768, kHashBits = 15, kMaxChain = 32, kMinMatch = 3, kMaxMatch = 258;
    std::vector<uint8_t> out = {0x78, 0x01};
    BitWriter w(out);
    w.Bits(1, 1);  // último bloco
    w.Bits(1, 2);  // Huffman fixo

    const int n = int(raw.size());
    std::vector<int> head(1 << kHashBits, -1), prev(kWindow, -1);
    auto hash = [&](int i) {
        return ((uint32_t(raw[i]) << 10) ^ (uint32_t(raw[i + 1]) << 5) ^ raw[i + 2]) & ((1u << kHashBits) - 1);
    };
    auto insert = [&](int i) {
        if (i + kMinMatch > n) return;
        const uint32_t h = hash(i);
        prev[i % kWindow] = head[h];
        head[h] = i;
    };
    int i = 0;
    while (i < n) {
        int bestLen = 0, bestDist = 0;
        if (i + kMinMatch <= n) {
            int candidate = head[hash(i)];
            const int maxLen = std::min(kMaxMatch, n - i);
            for (int chain = 0; candidate >= 0 && i - candidate <= kWindow && chain < kMaxChain; ++chain) {
                int len = 0;
                while (len < maxLen && raw[candidate + len] == raw[i + len]) ++len;
                if (len > bestLen) {
                    bestLen = len;
                    bestDist = i - candidate;
                    if (len == maxLen) break;
                }
                const int next = prev[candidate % kWindow];
                if (next >= candidate) break;  // entrada sobrescrita pela janela
                candidate = next;
            }
        }
        if (bestLen >= kMinMatch) {
            Match(w, bestLen, bestDist);
            for (int k = 0; k < bestLen; ++k) insert(i + k);
            i += bestLen;
        } else {
            FixedLiteral(w, raw[i]);
            insert(i);
            ++i;
        }
    }
    FixedLiteral(w, 256);  // fim do bloco
    w.Flush();

    uint32_t a = 1, b = 0;
    for (uint8_t byte : raw) {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    PutU32(out, (b << 16) | a);
    return out;
}

uint8_t Paeth(int a, int b, int c) {
    const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    return uint8_t(pa <= pb && pa <= pc ? a : pb <= pc ? b : c);
}
} // namespace

Image Downscale(const Image& image, int maxWidth, int maxHeight) {
    const double scale = std::min({1.0, double(maxWidth) / std::max(1, image.width),
                                   double(maxHeight) / std::max(1, image.height)});
    if (scale >= 1.0) return image;
    Image out;
    out.width = std::max(1, int(std::lround(image.width * scale)));
    out.height = std::max(1, int(std::lround(image.height * scale)));
    out.rgba.resize(size_t(out.width) * out.height * 4);
    for (int y = 0; y < out.height; ++y) {
        const int y0 = y * image.height / out.height, y1 = std::max(y0 + 1, (y + 1) * image.height / out.height);
        for (int x = 0; x < out.width; ++x) {
            const int x0 = x * image.width / out.width, x1 = std::max(x0 + 1, (x + 1) * image.width / out.width);
            uint32_t sum[4] = {};
            for (int sy = y0; sy < y1; ++sy) {
                for (int sx = x0; sx < x1; ++sx) {
                    const uint8_t* p = &image.rgba[(size_t(sy) * image.width + sx) * 4];
                    for (int c = 0; c < 4; ++c) sum[c] += p[c];
                }
            }
            const uint32_t n = uint32_t((y1 - y0) * (x1 - x0));
            uint8_t* q = &out.rgba[(size_t(y) * out.width + x) * 4];
            for (int c = 0; c < 4; ++c) q[c] = uint8_t(sum[c] / n);
        }
    }
    return out;
}

std::vector<uint8_t> EncodePng(const Image& image) {
    // Cada linha: o filtro do PNG que deixa os bytes menores (heurística padrão: menor soma absoluta) + RGB
    // filtrado. Filtrar transforma degradês e áreas lisas em zeros repetidos, que o deflate comprime bem.
    const size_t stride = size_t(image.width) * 3;
    std::vector<uint8_t> raw, line(stride), previous(stride, 0), best(stride), candidate(stride);
    raw.reserve(size_t(image.height) * (1 + stride));
    for (int y = 0; y < image.height; ++y) {
        const uint8_t* row = &image.rgba[size_t(y) * image.width * 4];
        for (int x = 0; x < image.width; ++x) std::copy(row + x * 4, row + x * 4 + 3, line.begin() + x * 3);
        int bestFilter = 0;
        long bestScore = -1;
        for (int filter = 0; filter < 5; ++filter) {
            long score = 0;
            for (size_t i = 0; i < stride; ++i) {
                const int a = i >= 3 ? line[i - 3] : 0, b = previous[i], c = i >= 3 ? previous[i - 3] : 0;
                const int predictor = filter == 0 ? 0 : filter == 1 ? a : filter == 2 ? b
                                    : filter == 3 ? (a + b) / 2 : Paeth(a, b, c);
                candidate[i] = uint8_t(line[i] - predictor);
                score += candidate[i] < 128 ? candidate[i] : 256 - candidate[i];
            }
            if (bestScore < 0 || score < bestScore) {
                bestScore = score;
                bestFilter = filter;
                best.swap(candidate);
            }
        }
        raw.push_back(uint8_t(bestFilter));
        raw.insert(raw.end(), best.begin(), best.end());
        previous.swap(line);
    }

    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<uint8_t> header;
    PutU32(header, uint32_t(image.width));
    PutU32(header, uint32_t(image.height));
    header.insert(header.end(), {8, 2, 0, 0, 0});  // 8 bits, RGB, deflate, filtro padrão, sem entrelaçamento
    Chunk(png, "IHDR", header);
    Chunk(png, "IDAT", Zlib(raw));
    Chunk(png, "IEND", {});
    return png;
}

std::string Base64(const std::vector<uint8_t>& data) {
    static constexpr char kChars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    for (size_t i = 0; i < data.size(); i += 3) {
        const uint32_t n = (uint32_t(data[i]) << 16) | (i + 1 < data.size() ? uint32_t(data[i + 1]) << 8 : 0) |
                           (i + 2 < data.size() ? uint32_t(data[i + 2]) : 0);
        out.push_back(kChars[(n >> 18) & 63]);
        out.push_back(kChars[(n >> 12) & 63]);
        out.push_back(i + 1 < data.size() ? kChars[(n >> 6) & 63] : '=');
        out.push_back(i + 2 < data.size() ? kChars[n & 63] : '=');
    }
    return out;
}

} // namespace editor::mcp

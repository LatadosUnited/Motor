// Camadas da engine: uma camada só inclui headers dela mesma e das camadas de baixo (ordem em
// engine/CMakeLists.txt, MOTOR_LAYERS). Assim cada camada pode ser mexida sem esbarrar nas de cima.
#include "TestUtil.h"

#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
std::vector<std::string> Split(const std::string& list) {
    std::vector<std::string> out;
    std::stringstream ss(list);
    for (std::string item; std::getline(ss, item, ',');) {
        if (!item.empty()) out.push_back(item);
    }
    return out;
}
} // namespace

int main() {
    const fs::path engine = MOTOR_ENGINE_DIR;
    const std::vector<std::string> layers = Split(MOTOR_LAYERS);
    std::map<std::string, int> rank;
    for (size_t i = 0; i < layers.size(); ++i) rank[layers[i]] = static_cast<int>(i);

    SECTION("Pastas das camadas");
    CHECK(!layers.empty());
    std::vector<std::string> unknown;
    for (const fs::path& root : {engine / "src", engine / "include" / "motor"}) {
        for (const auto& entry : fs::directory_iterator(root)) {
            if (entry.is_directory() && !rank.contains(entry.path().filename().string())) {
                unknown.push_back(entry.path().generic_string());
            }
        }
    }
    for (const auto& dir : unknown) std::printf("  pasta fora de MOTOR_LAYERS: %s\n", dir.c_str());
    CHECK(unknown.empty());

    SECTION("Includes só para a mesma camada ou para baixo");
    // "motor/<camada>/..." (headers públicos) ou "<camada>/..." entre aspas (headers privados de src/).
    const std::regex include(R"re(^\s*#\s*include\s*(?:"(?:motor/)?|<motor/)(\w+)/)re");
    std::vector<std::string> violations;
    int files = 0;
    for (const fs::path& root : {engine / "src", engine / "include" / "motor"}) {
        for (const auto& entry : fs::recursive_directory_iterator(root)) {
            if (!entry.is_regular_file()) continue;
            const std::string ext = entry.path().extension().string();
            if (ext != ".h" && ext != ".cpp" && ext != ".inl") continue;
            const fs::path relative = fs::relative(entry.path(), root);
            if (relative.begin() == relative.end() || std::next(relative.begin()) == relative.end()) continue;  // Motor.h
            const std::string layer = relative.begin()->string();
            if (!rank.contains(layer)) continue;  // já reportado acima
            ++files;

            std::ifstream in(entry.path());
            int lineNumber = 0;
            for (std::string line; std::getline(in, line);) {
                ++lineNumber;
                std::smatch m;
                if (!std::regex_search(line, m, include)) continue;
                const std::string target = m[1].str();
                if (rank.contains(target) && rank[target] > rank[layer]) {
                    violations.push_back(std::format("{}:{}: '{}' inclui a camada de cima '{}'",
                                                     fs::relative(entry.path(), engine).generic_string(),
                                                     lineNumber, layer, target));
                }
            }
        }
    }
    for (const auto& v : violations) std::printf("  %s\n", v.c_str());
    std::printf("  %d arquivos conferidos\n", files);
    CHECK(files > 0);
    CHECK(violations.empty());

    return TestResult();
}

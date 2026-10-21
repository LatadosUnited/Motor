// Teste de desempenho (Fase 22): mede a CPU do Motor na cena de referência (bench/referencia, ~5000 entidades) e
// falha se algum número piorar mais que a margem em relação à linha de base (bench/referencia/base.txt).
//
// Só em Release (em Debug os números não dizem nada): fora dele o teste se declara pulado. Sem janela nem GPU: mede o
// passo do jogo (física, scripts, áudio, UI) e o que o editor faz com a cena (copiar, comparar, gravar, ler).
//
//   ctest --test-dir build-release -L desempenho --output-on-failure
//   MOTOR_DESEMPENHO_GRAVAR=1 ...         grava os números de agora como a nova linha de base
//   test_desempenho.exe <pasta> --gerar "<projeto>"   grava Assets/Scenes/referencia.mscene no projeto (qualquer build)
#include "TestUtil.h"

#include "CenaReferencia.h"

#include <motor/assets/AssetLibrary.h>
#include <motor/core/Paths.h>
#include <motor/renderer/Mesh.h>
#include <motor/scene/Component.h>
#include <motor/scene/Scene.h>
#include <motor/scene/SceneRuntime.h>
#include <motor/scene/SceneSerializer.h>
#include <motor/scene/ScriptModule.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace motor;

namespace {

#ifdef NDEBUG
constexpr bool kRelease = true;
#else
constexpr bool kRelease = false;
#endif
constexpr int kSkipCode = 77;        // SKIP_RETURN_CODE no tests/CMakeLists.txt
constexpr double kMargem = 0.30;     // 30% acima da base = falha
constexpr double kFolgaMs = 0.05;    // números muito pequenos: o ruído é maior que a medida
constexpr int kRepeticoes = 3;       // de cada número fica a menor das repetições (a com menos interferência)

// A mesma lógica do bench/motor/Scripts/Bobber.cpp (que é o que a cena usa no projeto).
struct Bobber : Script {
    void OnStart() override {
        m_base = GetEntity().transform.position;
        m_fase = m_base.x * 0.1f + m_base.z * 0.07f;
    }
    void OnUpdate(float) override {
        auto& t = GetEntity().transform;
        t.position.y = m_base.y + std::sin(static_cast<float>(Time().elapsed) * 2.0f + m_fase) * 0.5f;
    }
    glm::vec3 m_base{0.0f};
    float m_fase = 0.0f;
};

using Clock = std::chrono::steady_clock;

double Ms(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

double Median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// Mediana de `n` execuções de fn.
double Measure(int n, const std::function<void()>& fn) {
    std::vector<double> ms;
    for (int i = 0; i < n; ++i) {
        const auto t0 = Clock::now();
        fn();
        ms.push_back(Ms(t0, Clock::now()));
    }
    return Median(ms);
}

AssetLibrary MakeAssets() {
    AssetLibrary assets;
    assets.AddMesh("Cubo", Mesh::CreateCube());  // sem GPU: a malha só sobe no primeiro desenho
    assets.AddMesh("Esfera", Mesh::CreateSphere());
    return assets;
}

// O que a montagem dos desenhos faz por entidade a cada quadro: "ativa na hierarquia" e matriz de mundo.
float MundoDeTodas(const Scene& scene) {
    float soma = 0.0f;
    for (const auto& e : scene.Entities()) {
        if (scene.IsActiveInHierarchy(e->id)) soma += scene.WorldMatrix(e->id)[3][0];
    }
    return soma;
}

// Hierarquia funda, como a de um modelo importado: `cadeias` cadeias de `fundura` entidades, cada uma filha da
// anterior, com um giro e um passo em cada nível.
void MontarArvores(Scene& scene, int cadeias, int fundura) {
    for (int c = 0; c < cadeias; ++c) {
        EntityId pai = kNullEntity;
        for (int d = 0; d < fundura; ++d) {
            Entity& e = scene.CreateEntity("No", pai);
            e.transform.position = {pai == kNullEntity ? static_cast<float>(c) : 0.0f, 0.5f, 0.0f};
            e.transform.Rotate({0.0f, 1.0f, 0.0f}, 0.1f);
            pai = e.id;
        }
    }
}

std::map<std::string, double> Run(AssetLibrary& assets, size_t& entidades) {
    std::map<std::string, double> r;
    Scene scene;
    r["cena_montar"] = Measure(3, [&] {
        scene = Scene();
        bench::MontarCenaReferencia(scene, assets);
    });
    entidades = scene.Entities().size();

    // O que o editor faz com a cena: cópia (desfazer, Play), comparação (desfazer a cada quadro), JSON (salvar e a
    // checagem de "não salvo"), leitura (abrir).
    Scene copia;
    r["cena_copiar"] = Measure(5, [&] { copia = scene; });
    bool iguais = false;
    r["cena_comparar"] = Measure(5, [&] { iguais = copia == scene; });
    CHECK(iguais);
    std::string json;
    r["cena_json"] = Measure(3, [&] { json = SceneSerializer::ToJson(scene, assets); });
    r["cena_ler"] = Measure(3, [&] {
        Scene lida;
        SceneSerializer::FromJson(json, lida, assets);
    });

    // Matrizes de mundo: na cena de referência (quase tudo na raiz) e numa hierarquia funda (200 cadeias de 10).
    volatile float soma = 0.0f;
    r["mundo_cena"] = Measure(21, [&] { soma = soma + MundoDeTodas(scene); });
    Scene arvores;
    MontarArvores(arvores, 200, 10);
    r["mundo_hierarquia"] = Measure(21, [&] { soma = soma + MundoDeTodas(arvores); });

    // O passo do jogo: 1 FixedUpdate + 1 Update por quadro, depois de 2 s (as caixas caem, empilham e dormem).
    SceneRuntime runtime(scene);
    runtime.SetAssets(&assets);
    runtime.Start();
    constexpr float kDt = 1.0f / 60.0f;
    for (int i = 0; i < 120; ++i) {
        runtime.FixedUpdate(kDt);
        runtime.Update(kDt);
    }
    r["jogo_passo"] = Measure(300, [&] {
        runtime.FixedUpdate(kDt);
        runtime.Update(kDt);
    });
    CHECK(!runtime.HasError());
    runtime.Stop();
    return r;
}

std::map<std::string, double> ReadBase(const std::filesystem::path& file) {
    std::map<std::string, double> base;
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string name;
        double value = 0.0;
        if (ss >> name >> value) base[name] = value;
    }
    return base;
}

bool WriteBase(const std::filesystem::path& file, const std::map<std::string, double>& values) {
    std::ofstream out(file, std::ios::binary);
    out << "# Linha de base do test_desempenho (ms, Release; a menor de " << kRepeticoes << " repeticoes).\n"
        << "# Regrave com MOTOR_DESEMPENHO_GRAVAR=1 ao trocar de maquina ou depois de uma melhora medida.\n";
    for (const auto& [name, value] : values) out << name << " " << value << "\n";
    return static_cast<bool>(out);
}

} // namespace

int main(int argc, char** argv) {
    ComponentRegistry::Instance().Register<Bobber>("Bobber", "Bobber");
    AssetLibrary assets = MakeAssets();

    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--gerar") != 0) continue;
        const std::filesystem::path project = PathFromUtf8(argv[i + 1]);
        Scene scene;
        bench::MontarCenaReferencia(scene, assets);
        const auto file = project / "Assets" / "Scenes" / "referencia.mscene";
        std::filesystem::create_directories(file.parent_path());
        std::string error;
        const bool ok = SceneSerializer::Save(file, scene, assets, &error);
        std::printf("%s: %zu entidades em %s %s\n", ok ? "Gravada" : "Falhou", scene.Entities().size(),
                    ToUtf8(file).c_str(), error.c_str());
        return ok ? 0 : 1;
    }

    if (!kRelease) {
        std::printf("Pulado: o teste de desempenho só vale em Release (ctest --test-dir build-release -L desempenho).\n");
        return kSkipCode;
    }
    const std::filesystem::path baseFile = std::filesystem::path(MOTOR_BENCH_DIR) / "referencia" / "base.txt";

    SECTION("Cena de referência");
    std::map<std::string, double> best;
    size_t entidades = 0;
    for (int rep = 0; rep < kRepeticoes; ++rep) {
        for (const auto& [name, ms] : Run(assets, entidades)) {
            best[name] = rep == 0 ? ms : std::min(best[name], ms);
        }
    }
    CHECK(entidades > bench::kEntidadesReferencia * 9 / 10 && entidades < bench::kEntidadesReferencia * 11 / 10);

    const char* gravar = std::getenv("MOTOR_DESEMPENHO_GRAVAR");
    std::map<std::string, double> base = ReadBase(baseFile);
    if ((gravar && gravar[0] == '1') || base.empty()) {
        CHECK(WriteBase(baseFile, best));
        std::printf("Linha de base gravada em %s\n", ToUtf8(baseFile).c_str());
        base = best;
    }

    SECTION("Comparação com a linha de base (falha acima de +30%)");
    std::printf("  %-16s %10s %10s %8s\n", "medida", "agora ms", "base ms", "");
    for (const auto& [name, ms] : best) {
        const auto it = base.find(name);
        if (it == base.end()) {
            std::printf("  %-16s %10.3f %10s   (nova: regrave a base)\n", name.c_str(), ms, "-");
            continue;
        }
        const double limit = it->second * (1.0 + kMargem) + kFolgaMs;
        std::printf("  %-16s %10.3f %10.3f %+7.0f%%%s\n", name.c_str(), ms, it->second,
                    (ms / it->second - 1.0) * 100.0, ms > limit ? "  PIOROU" : "");
        if (ms > limit) {
            std::printf("  FAIL %s: %.3f ms passou do limite de %.3f ms\n", name.c_str(), ms, limit);
            ++g_failures;
        }
    }
    return TestResult();
}

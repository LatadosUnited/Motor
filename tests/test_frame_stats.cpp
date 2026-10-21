// Contadores por quadro (Fase 22): FrameStats (somas no quadro, tudo zerado ao fechar), PerfRecorder (CSV do Player
// --perf, colunas que aparecem no meio, mediana sem o aquecimento) e a contabilidade da memória de vídeo (GpuMemory).
#include "TestUtil.h"

#include <motor/core/FrameStats.h>
#include <motor/core/Paths.h>
#include <motor/renderer/GpuMemory.h>

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

using namespace motor;

namespace {
double Find(const std::vector<FrameStats::Entry>& list, const std::string& name, double missing = -1.0) {
    for (const auto& e : list) {
        if (e.name == name) return e.value;
    }
    return missing;
}

bool Near(double a, double b) {
    return std::abs(a - b) < 1e-9;
}
} // namespace

int main(int argc, char** argv) {
    const auto dir = TestDir(argc, argv, "tmp_test_frame_stats");
    FrameStats& stats = FrameStats::Instance();

    SECTION("CPU e GPU somam no quadro; ao fechar, tudo volta a zero e as colunas ficam");
    {
        stats.AddCpu("Fisica", 1.0);
        stats.AddCpu("Fisica", 2.0);  // dois passos fixos no mesmo quadro
        stats.AddCpu("Update", 0.5);
        stats.AddGpu("Sombras", 3.0);
        stats.AddGpu("Sombras", 1.0);  // duas leituras da mesma passada no mesmo quadro (editor: duas vistas)
        stats.SetValue("draws", 42);
        stats.EndFrame(16.0);
        const FrameStats::Frame& f1 = stats.Last();
        CHECK(f1.index == 1);
        CHECK(Near(f1.frameMs, 16.0));
        CHECK(Near(Find(f1.cpu, "Fisica"), 3.0));
        CHECK(Near(Find(f1.cpu, "Update"), 0.5));
        CHECK(Near(Find(f1.gpu, "Sombras"), 4.0));
        CHECK(Near(Find(f1.values, "draws"), 42.0));

        stats.AddCpu("Update", 0.25);  // Fisica não rodou neste quadro
        stats.EndFrame(17.0);
        const FrameStats::Frame& f2 = stats.Last();
        CHECK(f2.index == 2 && f2.time >= f1.time);
        CHECK(Near(Find(f2.cpu, "Fisica"), 0.0));   // a coluna fica, zerada
        CHECK(Near(Find(f2.cpu, "Update"), 0.25));
        CHECK(Near(Find(f2.gpu, "Sombras"), 0.0));  // sem leitura neste quadro (ex.: janela minimizada)
        CHECK(Near(Find(f2.values, "draws"), 0.0));

        stats.AddGpu("Sombras", 2.5);  // não soma com a do quadro anterior
        stats.EndFrame(15.0);
        CHECK(Near(Find(stats.Last().gpu, "Sombras"), 2.5));
    }

    SECTION("CpuStatTimer mede o escopo");
    {
        {
            const CpuStatTimer timer("Escopo");
            volatile double x = 0.0;
            for (int i = 0; i < 100000; ++i) x = x + std::sqrt(static_cast<double>(i));
        }
        stats.EndFrame(1.0);
        CHECK(Find(stats.Last().cpu, "Escopo") > 0.0);
    }

    SECTION("PerfRecorder: CSV com colunas que aparecem no meio e mediana sem o aquecimento");
    {
        PerfRecorder rec;
        FrameStats::Frame a;
        a.index = 1;
        a.time = 0.0;
        a.frameMs = 50.0;  // aquecimento
        a.cpu = {{"Update", 1.5}};
        rec.Record(a);
        FrameStats::Frame b;
        b.index = 2;
        b.time = 5.0;
        b.frameMs = 16.0;
        b.cpu = {{"Update", 1.25}, {"Fisica", 0.5}};
        b.gpu = {{"Sombras", 2.0}};
        b.values = {{"draws", 10}};
        rec.Record(b);
        FrameStats::Frame c = b;
        c.index = 3;
        c.time = 6.0;
        c.frameMs = 18.0;
        rec.Record(c);

        CHECK(rec.FrameCount() == 3);
        const std::string csv = rec.ToCsv();
        std::istringstream lines(csv);
        std::string header, row1, row2;
        std::getline(lines, header);
        std::getline(lines, row1);
        std::getline(lines, row2);
        CHECK(header == "quadro,tempo_s,quadro_ms,cpu Update,cpu Fisica,gpu Sombras,draws");
        CHECK(row1 == "1,0.0000,50.000,1.500,,,");  // colunas que só apareceram depois: vazias
        CHECK(row2 == "2,5.0000,16.000,1.250,0.500,2.000,10.000");
        CHECK(Near(rec.MedianFrameMs(4.0), 18.0));  // 16 e 18: a do meio de cima
        CHECK(Near(rec.MedianFrameMs(0.0), 18.0));  // 16, 18, 50

        const auto file = dir / PathFromUtf8("Medição Ação") / "perf.csv";  // pasta com acento
        std::filesystem::create_directories(file.parent_path());
        std::string error;
        CHECK(rec.Save(file, &error));
        std::ifstream in(file, std::ios::binary);
        const std::string saved((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(saved == csv);
    }

    SECTION("Memória de vídeo: tamanho das texturas e registro por tipo de recurso (sem GPU)");
    {
        constexpr unsigned kRgba8 = 0x8058, kRgba16f = 0x881A, kDepth32f = 0x8CAC;  // GLenum
        CHECK(GpuTextureBytes(kRgba8, 256, 256) == 256ull * 256 * 4);
        CHECK(GpuTextureBytes(kRgba8, 4, 4, 1, 3) == (16ull + 4 + 1) * 4);       // mips 4x4, 2x2, 1x1
        CHECK(GpuTextureBytes(kRgba16f, 64, 64, 6, 1) == 64ull * 64 * 6 * 8);  // cubo
        CHECK(GpuTextureBytes(kDepth32f, 2048, 2048, 4) == 2048ull * 2048 * 4 * 4);  // 4 cascatas: 64 MB
        CHECK(GpuTextureBytes(kRgba8, 100, 50, 1, 1, 4) == 100ull * 50 * 4 * 4);     // MSAA 4x

        const GpuMemoryUsage before = CurrentGpuMemory();
        const auto tex = static_cast<size_t>(GpuMemoryKind::Textures);
        const auto mesh = static_cast<size_t>(GpuMemoryKind::Meshes);
        TrackGpuMemory(GpuObject::Texture, 7, GpuMemoryKind::Textures, 1000);
        TrackGpuMemory(GpuObject::Buffer, 7, GpuMemoryKind::Meshes, 300);  // mesmo número, outro tipo de objeto
        GpuMemoryUsage now = CurrentGpuMemory();
        CHECK(now.bytes[tex] == before.bytes[tex] + 1000 && now.objects[tex] == before.objects[tex] + 1);
        CHECK(now.bytes[mesh] == before.bytes[mesh] + 300);
        CHECK(now.Total() == before.Total() + 1300);

        TrackGpuMemory(GpuObject::Texture, 7, GpuMemoryKind::Textures, 400);  // registrar de novo troca
        CHECK(CurrentGpuMemory().bytes[tex] == before.bytes[tex] + 400);
        UntrackGpuMemory(GpuObject::Texture, 7);
        UntrackGpuMemory(GpuObject::Texture, 7);   // duas vezes: a segunda não faz nada
        UntrackGpuMemory(GpuObject::Buffer, 999);  // nunca registrado
        TrackGpuMemory(GpuObject::Texture, 0, GpuMemoryKind::Textures, 50);  // nome 0: ignorado
        UntrackGpuMemory(GpuObject::Buffer, 7);
        now = CurrentGpuMemory();
        CHECK(now.Total() == before.Total());
        CHECK(now.objects[tex] == before.objects[tex] && now.objects[mesh] == before.objects[mesh]);
        CHECK(std::string(GpuMemoryKindName(GpuMemoryKind::PostProcess)) == "pos-processamento");
    }

    return TestResult();
}

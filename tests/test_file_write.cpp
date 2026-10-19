// Gravação segura (motor/core/FileWrite.h): escreve o .tmp e troca pelo original, tentando de novo se o arquivo
// estiver preso por um instante (antivírus, indexador, outro programa lendo), sem nunca apagar o original antes.
#include "TestUtil.h"

#include <motor/core/FileWrite.h>
#include <motor/core/Paths.h>

#include <atomic>
#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;
using namespace motor;

namespace {
std::string ReadAll(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Segura o arquivo aberto (como um antivírus lendo) por `ms` milissegundos, numa thread.
std::thread HoldOpen(const fs::path& file, int ms, std::atomic<bool>& opened) {
    return std::thread([file, ms, &opened] {
        std::ifstream in(file, std::ios::binary);
        opened = in.is_open();
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    });
}

void WaitOpened(const std::atomic<bool>& opened) {
    for (int i = 0; i < 200 && !opened; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
}
} // namespace

int main(int argc, char** argv) {
    const fs::path dir = TestDir(argc, argv, "test_file_write_tmp");
    const fs::path file = dir / PathFromUtf8("Cena Ação.mscene");
    std::string error;

    SECTION("Arquivo novo e substituição");
    CHECK(WriteFileAtomic(file, "um", &error) && ReadAll(file) == "um");
    CHECK(WriteFileAtomic(file, "dois", &error) && ReadAll(file) == "dois");
    CHECK(!fs::exists(dir / PathFromUtf8("Cena Ação.mscene.tmp")));

#ifdef _WIN32
    SECTION("Destino preso por um instante: tenta de novo e grava");
    {
        std::atomic<bool> opened{false};
        std::thread holder = HoldOpen(file, 150, opened);
        WaitOpened(opened);
        CHECK(opened.load());
        const bool ok = WriteFileAtomic(file, "três", &error);
        if (!ok) std::printf("  erro: %s\n", error.c_str());
        holder.join();
        CHECK(ok && ReadAll(file) == "três");
    }

    SECTION("Destino preso por muito tempo: falha com a mensagem, o original fica inteiro e o .tmp some");
    {
        std::atomic<bool> opened{false};
        std::thread holder = HoldOpen(file, 3000, opened);
        WaitOpened(opened);
        error.clear();
        const bool ok = WriteFileAtomic(file, "quatro", &error);
        holder.join();
        std::printf("  (mensagem: %s)\n", error.c_str());
        CHECK(!ok && error.find("Cena Ação.mscene") != std::string::npos && error.find(": ") != std::string::npos);
        CHECK(ReadAll(file) == "três");
        CHECK(!fs::exists(dir / PathFromUtf8("Cena Ação.mscene.tmp")));
    }

    // O caso perigoso do código antigo: o antivírus segura o arquivo novo (.tmp) e a troca falha; antes, o original
    // era apagado para tentar de novo e, se a segunda troca também falhasse, a cena sumia.
    SECTION("Arquivo novo preso: ReplaceFileAtomic espera; preso demais, os dois arquivos ficam como estavam");
    {
        const fs::path from = dir / "novo.tmp";
        std::ofstream(from, std::ios::binary) << "novo";
        std::atomic<bool> opened{false};
        std::thread holder = HoldOpen(from, 150, opened);
        WaitOpened(opened);
        std::error_code ec;
        const bool ok = ReplaceFileAtomic(from, file, ec);
        holder.join();
        CHECK(ok && !ec && ReadAll(file) == "novo" && !fs::exists(from));

        std::ofstream(from, std::ios::binary) << "outro";
        opened = false;
        holder = HoldOpen(from, 3000, opened);
        WaitOpened(opened);
        CHECK(!ReplaceFileAtomic(from, file, ec) && ec);
        holder.join();
        CHECK(ReadAll(file) == "novo" && ReadAll(from) == "outro");  // nada apagado
    }
#endif

    SECTION("Pasta que não existe: erro ao escrever o .tmp");
    CHECK(!WriteFileAtomic(dir / "nao" / "existe.txt", "x", &error) && error.find("existe.txt.tmp") != std::string::npos);

    return TestResult();
}

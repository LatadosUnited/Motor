// FolderWatcher do editor (Fase 27): avisa quando algo muda na pasta do projeto, para a varredura dos arquivos só
// rodar quando precisa. Sem janela.
#include "TestUtil.h"

#include "FolderWatcher.h"

#include <motor/core/Paths.h>

#include <chrono>
#include <fstream>
#include <thread>

namespace fs = std::filesystem;
using editor::FolderWatcher;

namespace {
// O aviso do sistema chega logo depois da gravação, mas não no mesmo instante: espera até 2 s.
bool ChangedSoon(FolderWatcher& watcher) {
    for (int i = 0; i < 200; ++i) {
        if (watcher.Changed()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}
} // namespace

int main(int argc, char** argv) {
    const fs::path root = TestDir(argc, argv, "test_folder_watcher_tmp");
    const fs::path folder = root / "Assets" / motor::PathFromUtf8("Pasta Ação");
    fs::create_directories(folder);

    FolderWatcher watcher;
    SECTION("Sem pasta: sempre mudou (quem chama confere como antes)");
    CHECK(watcher.Changed());
    CHECK(watcher.Changed());

    SECTION("A primeira chamada depois de Watch é true; parada, false");
    watcher.Watch(root);
    CHECK(watcher.Folder() == root);
    CHECK(watcher.Changed());
    CHECK(!watcher.Changed());

    SECTION("Arquivo novo numa subpasta com acento");
    std::ofstream(folder / "novo.txt") << "a";
    CHECK(ChangedSoon(watcher));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));  // avisos atrasados da mesma gravação
    watcher.Changed();
    CHECK(!watcher.Changed());

    SECTION("Arquivo apagado");
    fs::remove(folder / "novo.txt");
    CHECK(ChangedSoon(watcher));

    SECTION("Watch vazio para de vigiar");
    watcher.Watch({});
    CHECK(watcher.Folder().empty());
    CHECK(watcher.Changed());

    return TestResult();
}

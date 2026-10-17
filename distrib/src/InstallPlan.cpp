#include "InstallPlan.h"

#include <HubCore.h>
#include <nlohmann/json.hpp>

#include <format>
#include <fstream>
#include <sstream>

namespace distrib {

using json = nlohmann::ordered_json;
using hub::PathFromUtf8;
using hub::ToUtf8;

namespace {
void SetError(std::string* error, std::string message) {
    if (error) *error = std::move(message);
}

bool IsBadRoot(const fs::path& dir) {
    // Raiz de unidade, vazio ou relativo: nunca vai para a Lixeira inteiro.
    return dir.empty() || !dir.is_absolute() || dir.lexically_normal() == dir.root_path() ||
           !dir.has_relative_path();
}
} // namespace

std::optional<InstallRecord> InstallRecord::Load(const fs::path& file, std::string* error) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        SetError(error, std::format("'{}' não encontrado.", ToUtf8(file)));
        return std::nullopt;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    try {
        const json root = json::parse(ss.str());
        if (!root.is_object() || root.value("format", std::string()) != kFormat) {
            SetError(error, std::format("'{}' não é um registro de instalação do Hub.", ToUtf8(file)));
            return std::nullopt;
        }
        InstallRecord r;
        r.hubVersion = root.value("hubVersion", std::string());
        r.installed = root.value("installed", std::string());
        r.installDir = PathFromUtf8(root.value("installDir", std::string()));
        if (r.installDir.empty()) r.installDir = file.parent_path();
        r.dataDir = PathFromUtf8(root.value("dataDir", std::string()));
        for (const json& s : root.value("shortcuts", json::array())) {
            if (s.is_string()) r.shortcuts.push_back(PathFromUtf8(s.get<std::string>()));
        }
        r.registryKey = root.value("registryKey", std::string());
        for (const json& s : root.value("servers", json::array())) {
            if (s.is_string()) r.servers.push_back(s.get<std::string>());
        }
        return r;
    } catch (const json::exception& e) {
        SetError(error, std::format("'{}' inválido: {}", ToUtf8(file), e.what()));
        return std::nullopt;
    }
}

bool InstallRecord::Save(const fs::path& file, std::string* error) const {
    json root;
    root["format"] = kFormat;
    root["version"] = kVersion;
    root["hubVersion"] = hubVersion;
    root["installed"] = installed;
    root["installDir"] = ToUtf8(installDir);
    root["dataDir"] = ToUtf8(dataDir);
    json links = json::array();
    for (const fs::path& s : shortcuts) links.push_back(ToUtf8(s));
    root["shortcuts"] = std::move(links);
    root["registryKey"] = registryKey;
    root["servers"] = servers;

    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    fs::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out || !(out << root.dump(2, ' ', false, json::error_handler_t::replace) << '\n') || !out.flush()) {
            SetError(error, std::format("Não foi possível escrever '{}'.", ToUtf8(file)));
            return false;
        }
    }
    fs::rename(tmp, file, ec);
    if (ec) {
        fs::remove(tmp, ec);
        SetError(error, std::format("Não foi possível escrever '{}'.", ToUtf8(file)));
        return false;
    }
    return true;
}

std::string HubArguments(const fs::path& dataDir) {
    if (dataDir.empty()) return {};
    // Sem barra no fim: "C:\Dados\" fecharia as aspas como \" na leitura da linha de comando.
    std::string path = ToUtf8(dataDir);
    while (path.size() > 3 && (path.back() == '\\' || path.back() == '/')) path.pop_back();
    return "--data \"" + path + "\"";
}

InstallPlan PlanInstall(const InstallOptions& o, const fs::path& startMenuDir, const fs::path& desktopDir) {
    InstallPlan plan;
    const fs::path dir = o.installDir.lexically_normal();
    plan.hubExe = dir / kHubExe;
    plan.setupExe = dir / kSetupExe;
    plan.recordFile = dir / kInstallRecordFile;

    const auto shortcut = [&](const fs::path& folder) {
        Shortcut s;
        s.link = folder / kShortcutName;
        s.target = plan.hubExe;
        s.arguments = HubArguments(o.dataDir);
        s.workingDir = dir;
        s.description = "Hub do Motor: projetos e versões";
        plan.shortcuts.push_back(std::move(s));
    };
    if (o.startMenuShortcut && !startMenuDir.empty()) shortcut(startMenuDir);
    if (o.desktopShortcut && !desktopDir.empty()) shortcut(desktopDir);

    if (o.registerUninstall && !o.registryKey.empty()) {
        plan.registryKey = o.registryKey;
        const std::string setup = "\"" + ToUtf8(plan.setupExe) + "\"";
        const auto text = [&](const char* name, std::string value) {
            plan.registryValues.push_back({name, std::move(value), 0, false});
        };
        const auto number = [&](const char* name, uint32_t value) {
            plan.registryValues.push_back({name, {}, value, true});
        };
        text("DisplayName", "Motor Hub");
        text("DisplayVersion", o.hubVersion);
        text("Publisher", "Motor");
        text("DisplayIcon", ToUtf8(plan.hubExe));
        text("InstallLocation", ToUtf8(dir));
        text("UninstallString", setup + " --desinstalar");
        text("QuietUninstallString", setup + " --desinstalar --silencioso");
        number("NoModify", 1);
        number("NoRepair", 1);
        number("EstimatedSize", static_cast<uint32_t>(o.installedBytes / 1024));
    }

    InstallRecord& r = plan.record;
    r.hubVersion = o.hubVersion;
    r.installDir = dir;
    r.dataDir = o.dataDir;
    for (const Shortcut& s : plan.shortcuts) r.shortcuts.push_back(s.link);
    r.registryKey = plan.registryKey;
    if (!o.server.empty()) r.servers.push_back(o.server);
    return plan;
}

UninstallPlan PlanUninstall(const InstallRecord& record, bool removeData, const fs::path& defaultDataDir) {
    UninstallPlan plan;
    for (const fs::path& link : record.shortcuts) {
        if (link.extension() == ".lnk") plan.shortcuts.push_back(link);  // só atalhos
    }
    plan.registryKey = record.registryKey;

    std::error_code ec;
    const fs::path dir = record.installDir.lexically_normal();
    if (!IsBadRoot(dir) &&
        (fs::exists(dir / kInstallRecordFile, ec) || fs::exists(dir / kHubExe, ec))) {
        plan.toTrash.push_back(dir);
    }
    if (removeData) {
        const fs::path data = (record.dataDir.empty() ? defaultDataDir : record.dataDir).lexically_normal();
        if (!IsBadRoot(data)) {
            // Só o que é do hub: versões instaladas e a configuração. O resto da pasta fica.
            for (const fs::path& item : {data / PathFromUtf8("Versões"), data / "hub.json"}) {
                if (fs::exists(item, ec)) plan.toTrash.push_back(item);
            }
            plan.emptyDirToRemove = data;
        }
    }
    return plan;
}

std::vector<BuildTool> CheckBuildTools(const fs::path& sdkJson, const fs::path& msysRoot) {
    fs::path cxx = msysRoot / "ucrt64" / "bin" / "g++.exe";
    fs::path cmake = msysRoot / "ucrt64" / "bin" / "cmake.exe";
    std::ifstream in(sdkJson, std::ios::binary);
    if (in) {
        std::stringstream ss;
        ss << in.rdbuf();
        try {
            const json root = json::parse(ss.str());
            if (const std::string v = root.value("cxxCompiler", std::string()); !v.empty()) cxx = PathFromUtf8(v);
            if (const std::string v = root.value("cmake", std::string()); !v.empty()) cmake = PathFromUtf8(v);
        } catch (const json::exception&) {
        }
    }
    std::error_code ec;
    std::vector<BuildTool> tools;
    tools.push_back({"g++ (MSYS2 UCRT64)", cxx, fs::is_regular_file(cxx, ec)});
    tools.push_back({"CMake", cmake, fs::is_regular_file(cmake, ec)});
    return tools;
}

} // namespace distrib

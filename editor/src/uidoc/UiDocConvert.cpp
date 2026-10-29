#include "uidoc/UiDocConvert.h"

#include <motor/common/Text.h>
#include <motor/core/Paths.h>
#include <motor/ui/UiConvert.h>

#include <format>

namespace editor {

using namespace motor;
namespace fs = std::filesystem;

namespace {

// Nome de arquivo a partir do nome da entidade (sem os caracteres proibidos no Windows).
std::string FileStem(std::string name) {
    for (char& c : name) {
        if (std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos) c = '_';
    }
    while (!name.empty() && (name.back() == ' ' || name.back() == '.')) name.pop_back();
    return name.empty() ? std::string("Documento") : name;
}

} // namespace

bool HasConvertibleUi(const Scene& scene, EntityId id) {
    const Entity* e = scene.Find(id);
    if (!e || e->Has<UiDocument>()) return false;
    if (IsUiEntity(*e)) return true;
    for (EntityId child : scene.Children(id)) {
        const Entity* c = scene.Find(child);
        if (c && IsUiEntity(*c) && !c->Has<UiDocument>()) return true;
    }
    return false;
}

bool ConvertUiToDocument(EditorContext& ctx, EntityId root, const std::string& file, UiDocConvertResult& out,
                         std::string* error) {
    out = {};
    if (ctx.playing) {
        SetError(error, "pare o Play antes de converter (a conversão muda a cena do editor)");
        return false;
    }
    if (ctx.assets.Root().empty()) {
        SetError(error, "abra um projeto antes (o .mui fica em Assets/UI)");
        return false;
    }
    UiConversion conversion;
    if (!ConvertUiEntities(ctx.scene, root, ctx.assets, conversion, error)) return false;
    const Entity* rootEntity = ctx.scene.Find(root);
    const std::string name = rootEntity->name;

    // Arquivo: o pedido, ou Assets/UI/<nome>.mui sem sobrescrever um que já existe.
    fs::path path;
    if (!file.empty()) {
        path = ctx.assets.FileForKey(file);
        if (path.empty()) path = ctx.assets.Root() / PathFromUtf8(file);
    } else {
        const fs::path folder = ctx.assets.Root() / "Assets" / "UI";
        const std::string stem = FileStem(name);
        path = folder / PathFromUtf8(stem + ".mui");
        for (int i = 2; fs::exists(path); ++i) path = folder / PathFromUtf8(std::format("{} {}.mui", stem, i));
    }
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (!SaveUiDocument(path, conversion.document, error)) return false;
    ctx.assets.Database().GuidFor(path);  // cria o .meta
    out.key = ctx.assets.KeyForFile(path);
    out.elements = conversion.converted.size();

    // Cena: o documento no lugar das entidades de UI.
    EntityId target = root;
    if (conversion.rootIsUi) {
        const UiRect* rect = rootEntity->Get<UiRect>();
        const int order = rect ? rect->order : 0;
        const EntityId parent = rootEntity->parent;
        ctx.scene.DestroyEntity(root);
        Entity& created = ctx.scene.CreateEntity(name, parent);
        created.Add<UiDocument>().order = order;
        target = created.id;
    } else {
        std::vector<EntityId> tops;  // só as de cima (os filhos vão junto)
        for (EntityId id : conversion.converted) {
            const Entity* e = ctx.scene.Find(id);
            if (e && e->parent == root) tops.push_back(id);
        }
        ctx.scene.DestroyEntities(tops);
        ctx.scene.Find(root)->Add<UiDocument>();
    }
    UiDocument* document = ctx.scene.Find(target)->Get<UiDocument>();
    SetUiDocumentFile(document->document, out.key, ctx.assets);
    out.entity = target;
    ctx.SelectOnly(target);
    return true;
}

} // namespace editor

#include "FieldDrawers.h"

#include <string>
#include <unordered_map>

namespace editor {

namespace {
// Estática local: os registradores rodam na inicialização estática, em qualquer ordem entre os .cpp.
std::unordered_map<std::string, FieldDrawerFn>& Drawers() {
    static std::unordered_map<std::string, FieldDrawerFn> drawers;
    return drawers;
}
} // namespace

FieldDrawerRegistrar::FieldDrawerRegistrar(const char* typeName, FieldDrawerFn fn) {
    Drawers()[typeName] = fn;
}

FieldDrawerFn FindFieldDrawer(std::string_view typeName) {
    const auto it = Drawers().find(std::string(typeName));
    return it == Drawers().end() ? nullptr : it->second;
}

} // namespace editor

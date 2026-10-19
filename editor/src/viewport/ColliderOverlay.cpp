#include "viewport/ColliderOverlay.h"

namespace editor {

std::string_view ColliderOverlayName(ColliderOverlay mode) {
    switch (mode) {
        case ColliderOverlay::All: return "all";
        case ColliderOverlay::None: return "none";
        case ColliderOverlay::Selected: break;
    }
    return "selected";
}

ColliderOverlay ParseColliderOverlay(std::string_view name) {
    if (name == "all") return ColliderOverlay::All;
    if (name == "none") return ColliderOverlay::None;
    return ColliderOverlay::Selected;
}

const char* ColliderOverlayLabel(ColliderOverlay mode) {
    switch (mode) {
        case ColliderOverlay::All: return "Colliders: Todos";
        case ColliderOverlay::None: return "Colliders: Nenhum";
        case ColliderOverlay::Selected: break;
    }
    return "Colliders: Selecionado";
}

ColliderOverlay NextColliderOverlay(ColliderOverlay mode) {
    switch (mode) {
        case ColliderOverlay::Selected: return ColliderOverlay::All;
        case ColliderOverlay::All: return ColliderOverlay::None;
        case ColliderOverlay::None: break;
    }
    return ColliderOverlay::Selected;
}

bool ShouldDrawCollider(ColliderOverlay mode, bool selected, bool activeInHierarchy) {
    switch (mode) {
        case ColliderOverlay::None: return false;
        case ColliderOverlay::Selected: return selected;
        case ColliderOverlay::All: return selected || activeInHierarchy;
    }
    return false;
}

} // namespace editor

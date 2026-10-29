#pragma once

// Edição de um documento de UI (.mui) e das folhas (.mstyle) dele, sem janela nem OpenGL: a parte do Editor de UI
// (UiDocEditor) que dá para testar (tests/test_ui_editor.cpp). Os elementos são achados por caminho (índices desde a
// raiz), porque a árvore viva da prévia é uma cópia: o caminho do elemento clicado nela vale no documento editado.

#include <motor/ui/UiDocument.h>
#include <motor/ui/UiLayout.h>
#include <motor/ui/UiStyle.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace editor {

using UiPath = std::vector<int>;  // vazio = a raiz do documento (a lista de elementos de cima)

motor::UiElement* UiElementAt(std::vector<motor::UiElement>& roots, const UiPath& path);
const motor::UiElement* UiElementAt(const std::vector<motor::UiElement>& roots, const UiPath& path);
// A lista de filhos do elemento no caminho (vazio = a raiz). nullptr se o caminho não existe.
std::vector<motor::UiElement>* UiChildrenAt(std::vector<motor::UiElement>& roots, const UiPath& path);
// Caminho de um elemento (pelo endereço) na árvore. false se não estiver nela.
bool FindUiElementPath(const std::vector<motor::UiElement>& roots, const motor::UiElement* target, UiPath& out);
// Caminho do primeiro elemento com esse nome (em profundidade). false se nenhum.
bool FindUiElementPath(const std::vector<motor::UiElement>& roots, std::string_view name, UiPath& out);
bool IsUiPathPrefix(const UiPath& prefix, const UiPath& path);  // `prefix` é `path` ou um ancestral dele

// Nome que ainda não existe no documento: `base`, `base2`, `base3`... (vazio = "elemento").
std::string UniqueUiElementName(const std::vector<motor::UiElement>& roots, std::string_view base);
// Elemento novo da paleta, com nome único e um tamanho que aparece na prévia (painel com fundo, imagem 100x100...).
motor::UiElement NewPaletteElement(motor::UiElementType type, const std::vector<motor::UiElement>& roots);

// Operações na árvore. `index` < 0 ou além do fim = no fim. Devolvem o caminho novo (vazio = falhou).
UiPath InsertUiElement(std::vector<motor::UiElement>& roots, const UiPath& parent, int index, motor::UiElement element);
bool RemoveUiElement(std::vector<motor::UiElement>& roots, const UiPath& path);
// Cópia logo depois do original, com nomes únicos nela e nos filhos.
UiPath DuplicateUiElement(std::vector<motor::UiElement>& roots, const UiPath& path);
// Muda de pai ou de lugar entre os irmãos. `index` conta na lista do pai novo como ela fica sem o elemento movido.
// Recusa mover para dentro dele mesmo.
UiPath MoveUiElement(std::vector<motor::UiElement>& roots, const UiPath& from, const UiPath& newParent, int index);

// Classes como se digita no campo: "menu .grande, destaque" -> {"menu", "grande", "destaque"} (sem repetidas).
std::vector<std::string> ParseUiClassList(std::string_view text);
std::string JoinUiClassList(const std::vector<std::string>& classes);  // "menu grande"

// Redimensiona pelas alças da prévia: `from` e `to` são a caixa antes e depois, em pixels da referência (pixels da
// tela / UiScale). Grava width e height em pixels (só no eixo que mudou) e, posicionado por âncora, a posição que
// mantém o lado que não andou. Com âncora Stretch, position e width/height são as margens.
void ResizeUiElement(motor::UiElement& element, const motor::UiBox& from, const motor::UiBox& to);

// Seletor novo de uma regra (lido e validado). false com o erro, e a regra fica como estava.
bool SetUiRuleSelector(motor::UiStyleRule& rule, std::string_view text, std::string* error = nullptr);

// Folha .mstyle de um documento aberta para editar.
struct UiEditSheet {
    std::string key;                 // "Assets/UI/Tema.mstyle" (como no .mui)
    std::filesystem::path file;      // vazio = não achou o arquivo (só leitura)
    motor::UiStyleSheet sheet;
    bool operator==(const UiEditSheet&) const = default;
};

// Tudo o que o Editor de UI muda: o documento, as folhas e a seleção (o desfazer volta os três).
struct UiEditState {
    motor::UiDocumentData doc;
    std::vector<UiEditSheet> sheets;  // na ordem de doc.styles (as que abriram)
    UiPath selected;                  // vazio = nada selecionado

    bool SameContent(const UiEditState& other) const;  // documento e folhas (a seleção não conta)
};

// Põe as folhas editadas no documento (no lugar das lidas do disco) e recalcula os estilos.
void ApplyUiEditSheets(UiEditState& state);

// Desfazer do Editor de UI: fotos inteiras do estado (documentos são pequenos). O estado vivo é editado direto pela
// janela; Commit no fim de cada interação vira um passo se algo mudou. Version muda a cada passo, desfazer e refazer.
class UiEditHistory {
public:
    void Reset(const UiEditState& state);
    bool Commit(const UiEditState& state);  // true se virou um passo
    bool Undo(UiEditState& state);          // o estado vivo volta ao passo anterior (com o que não foi gravado ainda)
    bool Redo(UiEditState& state);
    bool CanUndo() const { return !m_undo.empty(); }
    bool CanRedo() const { return !m_redo.empty(); }
    uint64_t Version() const { return m_version; }
    const UiEditState& Current() const { return m_current; }

private:
    struct Step {
        UiEditState state;
        uint64_t version = 0;
    };
    static constexpr size_t kMaxSteps = 200;

    UiEditState m_current;
    uint64_t m_version = 0;  // o de m_current: voltar ao passo gravado volta à versão dele ("não salvo" some)
    uint64_t m_nextVersion = 1;
    std::vector<Step> m_undo;
    std::vector<Step> m_redo;
};

} // namespace editor

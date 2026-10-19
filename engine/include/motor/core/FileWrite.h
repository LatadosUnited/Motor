#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace motor {

// Gravação segura de arquivos importantes (projeto, cena, material, recuperação): escreve `<arquivo>.tmp` e só
// então troca pelo original. O original nunca é apagado antes de o novo estar no lugar: uma falha no meio deixa
// o arquivo antigo inteiro.
//
// No Windows o arquivo pode ficar preso por um instante logo depois de gravado (antivírus, indexador, outro
// programa lendo): a troca é tentada de novo por até ~1 s antes de desistir. Erro: `error` recebe o arquivo e a
// mensagem do sistema.
bool WriteFileAtomic(const std::filesystem::path& file, std::string_view data, std::string* error = nullptr);

// Só a troca, para quem já gravou o arquivo novo: `from` passa a ser `to` (substituindo), com as mesmas novas
// tentativas. `from` continua lá se falhar.
bool ReplaceFileAtomic(const std::filesystem::path& from, const std::filesystem::path& to, std::error_code& ec);

} // namespace motor

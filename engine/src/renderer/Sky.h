#pragma once

// Header privado do renderer. Céu e luz ambiente por imagem: cubemap do céu (cor ou HDR), reflexo
// pré-filtrado por rugosidade, tabela da BRDF e a irradiância em harmônicos esféricos (calculada na CPU).

#include "motor/renderer/Environment.h"
#include "motor/renderer/Pbr.h"
#include "motor/renderer/Shader.h"

#include <glm/vec3.hpp>

#include <memory>
#include <string>

namespace motor {

class PostProcess;

class SkyRenderer {
public:
    static constexpr int kEnvironmentSize = 256;  // face do cubemap do céu
    static constexpr int kPrefilterSize = 128;    // face do mip 0 do reflexo
    static constexpr int kPrefilterLevels = 6;    // rugosidade 0, 0.2, ..., 1

    explicit SkyRenderer(const std::string& shaderDir);
    ~SkyRenderer();

    SkyRenderer(const SkyRenderer&) = delete;
    SkyRenderer& operator=(const SkyRenderer&) = delete;

    // Refaz o que mudou (cubemaps quando a cor ou a imagem mudam; harmônicos quando também gira). Usa o seu
    // próprio framebuffer: chame antes de vincular o alvo da cena.
    void Update(const Environment& environment, const PostProcess& fullscreen);

    const Sh9& Irradiance() const { return m_irradiance; }  // sem a intensidade
    glm::vec3 HorizonColor() const { return m_horizon; }     // média do horizonte (neblina), sem a intensidade
    float MaxReflectionMip() const { return static_cast<float>(kPrefilterLevels - 1); }

    unsigned int EnvironmentCube() const { return m_environmentCube; }
    unsigned int PrefilteredCube() const { return m_prefilteredCube; }
    unsigned int BrdfLut() const { return m_brdfLut; }

    // Fundo: o cubemap do céu onde a profundidade ainda é 1 (depois dos objetos). Frame UBO já enviado.
    void DrawBackground(const PostProcess& fullscreen);

private:
    void BuildCubes(const Environment& environment, const PostProcess& fullscreen);
    void UploadEquirect(const HdrImage& image);

    std::unique_ptr<Shader> m_skyShader;
    std::unique_ptr<Shader> m_cubeShader;
    std::unique_ptr<Shader> m_prefilterShader;
    unsigned int m_fbo = 0;
    unsigned int m_environmentCube = 0;
    unsigned int m_prefilteredCube = 0;
    unsigned int m_brdfLut = 0;
    unsigned int m_equirect = 0;
    int m_equirectWidth = 0;

    // O que já está nos cubemaps e nos harmônicos.
    bool m_built = false;
    SkyMode m_mode = SkyMode::Color;
    glm::vec3 m_color{-1.0f};
    const HdrImage* m_image = nullptr;
    std::shared_ptr<const HdrImage> m_imageRef;  // mantém viva a imagem de m_image
    HdrImage m_small;                            // imagem reduzida (harmônicos e horizonte)
    float m_rotation = 0.0f;
    bool m_shBuilt = false;
    Sh9 m_irradiance;
    glm::vec3 m_horizon{0.0f};
};

} // namespace motor

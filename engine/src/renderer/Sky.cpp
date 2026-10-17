#include "renderer/Sky.h"

#include "renderer/GL.h"
#include "renderer/PostProcess.h"

#include <glm/common.hpp>
#include <glm/exponential.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <vector>

namespace motor {

namespace {
constexpr int kBrdfLutSize = 64;
constexpr int kSmallWidth = 128;  // imagem reduzida para os harmônicos (média por área: o sol não some)

HdrImage Downsample(const HdrImage& image, int width) {
    HdrImage out;
    out.width = std::min(width, image.width);
    out.height = std::max(1, std::min(out.width / 2, image.height));
    out.rgb.assign(static_cast<size_t>(out.width * out.height * 3), 0.0f);
    for (int y = 0; y < out.height; ++y) {
        const int y0 = y * image.height / out.height, y1 = std::max(y0 + 1, (y + 1) * image.height / out.height);
        for (int x = 0; x < out.width; ++x) {
            const int x0 = x * image.width / out.width, x1 = std::max(x0 + 1, (x + 1) * image.width / out.width);
            glm::vec3 sum(0.0f);
            for (int sy = y0; sy < y1; ++sy) {
                for (int sx = x0; sx < x1; ++sx) sum += image.Pixel(sx, sy);
            }
            const glm::vec3 avg = sum / static_cast<float>((x1 - x0) * (y1 - y0));
            float* p = &out.rgb[static_cast<size_t>((y * out.width + x) * 3)];
            p[0] = avg.r;
            p[1] = avg.g;
            p[2] = avg.b;
        }
    }
    return out;
}

GLuint CreateCube(int size, int levels) {
    GLuint cube = 0;
    glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &cube);
    glTextureStorage2D(cube, levels, GL_RGBA16F, size, size);
    glTextureParameteri(cube, GL_TEXTURE_MIN_FILTER, levels > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTextureParameteri(cube, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(cube, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(cube, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(cube, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    return cube;
}
} // namespace

SkyRenderer::SkyRenderer(const std::string& dir) {
    m_skyShader = Shader::FromFiles(dir + "fullscreen.vert", dir + "sky.frag");
    m_cubeShader = Shader::FromFiles(dir + "fullscreen.vert", dir + "sky_cube.frag");
    m_prefilterShader = Shader::FromFiles(dir + "fullscreen.vert", dir + "prefilter.frag");
    glCreateFramebuffers(1, &m_fbo);

    const int envLevels = static_cast<int>(std::bit_width(static_cast<unsigned>(kEnvironmentSize)));
    m_environmentCube = CreateCube(kEnvironmentSize, envLevels);
    m_prefilteredCube = CreateCube(kPrefilterSize, kPrefilterLevels);

    // Tabela da BRDF: calculada uma vez na CPU (a mesma função testada no ctest).
    const std::vector<glm::vec2> lut = BuildBrdfLut(kBrdfLutSize, 128);
    glCreateTextures(GL_TEXTURE_2D, 1, &m_brdfLut);
    glTextureStorage2D(m_brdfLut, 1, GL_RG16F, kBrdfLutSize, kBrdfLutSize);
    glTextureSubImage2D(m_brdfLut, 0, 0, 0, kBrdfLutSize, kBrdfLutSize, GL_RG, GL_FLOAT, lut.data());
    glTextureParameteri(m_brdfLut, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_brdfLut, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_brdfLut, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_brdfLut, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

SkyRenderer::~SkyRenderer() {
    glDeleteFramebuffers(1, &m_fbo);
    glDeleteTextures(1, &m_environmentCube);
    glDeleteTextures(1, &m_prefilteredCube);
    glDeleteTextures(1, &m_brdfLut);
    if (m_equirect) glDeleteTextures(1, &m_equirect);
}

void SkyRenderer::UploadEquirect(const HdrImage& image) {
    if (m_equirect) glDeleteTextures(1, &m_equirect);
    const auto levels = static_cast<GLsizei>(std::bit_width(static_cast<unsigned>(std::max(image.width, image.height))));
    glCreateTextures(GL_TEXTURE_2D, 1, &m_equirect);
    glTextureStorage2D(m_equirect, levels, GL_RGB16F, image.width, image.height);
    glTextureSubImage2D(m_equirect, 0, 0, 0, image.width, image.height, GL_RGB, GL_FLOAT, image.rgb.data());
    glTextureParameteri(m_equirect, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(m_equirect, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_equirect, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(m_equirect, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenerateTextureMipmap(m_equirect);
    m_equirectWidth = image.width;
}

void SkyRenderer::Update(const Environment& env, const PostProcess& fullscreen) {
    const bool useTexture = env.skyMode == SkyMode::Texture && env.skyTexture && env.skyTexture->width > 0 &&
                            env.skyTexture->rgb.size() >= static_cast<size_t>(env.skyTexture->width * env.skyTexture->height * 3);
    const SkyMode mode = useTexture ? SkyMode::Texture : SkyMode::Color;
    const glm::vec3 color = SrgbToLinear(env.skyColor);
    const HdrImage* image = useTexture ? env.skyTexture.get() : nullptr;

    const bool dirty = !m_built || mode != m_mode || (mode == SkyMode::Color && color != m_color) ||
                       (mode == SkyMode::Texture && image != m_image);
    if (dirty) {
        if (image && image != m_image) {
            UploadEquirect(*image);
            m_small = Downsample(*image, kSmallWidth);
        }
        m_mode = mode;
        m_color = color;
        m_image = image;
        m_imageRef = useTexture ? env.skyTexture : nullptr;
        BuildCubes(env, fullscreen);
        m_built = true;
        m_shBuilt = false;
    }

    if (!m_shBuilt || env.skyRotation != m_rotation) {
        m_rotation = env.skyRotation;
        const float rotation = glm::radians(m_rotation);
        auto radiance = [&](const glm::vec3& d) {
            return m_mode == SkyMode::Texture ? SampleEquirect(m_small, RotateY(d, -rotation)) : GradientSky(d, m_color);
        };
        m_irradiance = ConvolveIrradiance(ProjectSh9(radiance, 32));
        if (m_mode == SkyMode::Texture) {
            // Neblina: média do céu logo acima do horizonte (onde ela encontra o fundo).
            glm::vec3 sum(0.0f);
            int n = 0;
            for (int e = 0; e < 4; ++e) {
                const float y = 0.02f + 0.05f * static_cast<float>(e);
                const float r = std::sqrt(1.0f - y * y);
                for (int a = 0; a < 64; ++a) {
                    const float phi = 2.0f * kPi * static_cast<float>(a) / 64.0f;
                    sum += SampleEquirect(m_small, glm::vec3(r * std::cos(phi), y, r * std::sin(phi)));
                    ++n;
                }
            }
            m_horizon = sum / static_cast<float>(n);
        } else {
            m_horizon = m_color;
        }
        m_shBuilt = true;
    }
}

void SkyRenderer::BuildCubes(const Environment&, const PostProcess& fullscreen) {
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // Céu na resolução cheia, com os mips (o pré-filtro lê os mips borrados).
    m_cubeShader->Bind();
    m_cubeShader->SetInt("uMode", m_mode == SkyMode::Texture ? 1 : 0);
    m_cubeShader->SetVec3("uSkyColor", m_color);
    const float lod = m_equirectWidth > 0 ? std::max(0.0f, std::log2(static_cast<float>(m_equirectWidth) / (4.0f * kEnvironmentSize))) : 0.0f;
    m_cubeShader->SetFloat("uLod", lod);
    if (m_equirect) glBindTextureUnit(0, m_equirect);
    glViewport(0, 0, kEnvironmentSize, kEnvironmentSize);
    for (int face = 0; face < 6; ++face) {
        glNamedFramebufferTextureLayer(m_fbo, GL_COLOR_ATTACHMENT0, m_environmentCube, 0, face);
        m_cubeShader->SetInt("uFace", face);
        fullscreen.DrawFullscreen();
    }
    glGenerateTextureMipmap(m_environmentCube);

    // Reflexo: um mip por rugosidade.
    m_prefilterShader->Bind();
    m_prefilterShader->SetFloat("uSourceSize", static_cast<float>(kEnvironmentSize));
    glBindTextureUnit(0, m_environmentCube);
    for (int level = 0; level < kPrefilterLevels; ++level) {
        const int size = std::max(kPrefilterSize >> level, 1);
        glViewport(0, 0, size, size);
        m_prefilterShader->SetFloat("uRoughness", static_cast<float>(level) / static_cast<float>(kPrefilterLevels - 1));
        for (int face = 0; face < 6; ++face) {
            glNamedFramebufferTextureLayer(m_fbo, GL_COLOR_ATTACHMENT0, m_prefilteredCube, level, face);
            m_prefilterShader->SetInt("uFace", face);
            fullscreen.DrawFullscreen();
        }
    }
    glNamedFramebufferTexture(m_fbo, GL_COLOR_ATTACHMENT0, 0, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void SkyRenderer::DrawBackground(const PostProcess& fullscreen) {
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    m_skyShader->Bind();
    glBindTextureUnit(0, m_environmentCube);
    fullscreen.DrawFullscreen();
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
}

} // namespace motor

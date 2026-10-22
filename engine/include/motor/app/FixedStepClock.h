#pragma once

// Relógio dos passos fixos da Application: soma o tempo de cada quadro e diz quantos passos fixos rodar. Separado
// do loop para ser testado sem janela (tests/test_passo_fixo.cpp).

#include <algorithm>
#include <cmath>

namespace motor {

class FixedStepClock {
public:
    // `maxSteps`: passos no máximo por quadro (0 = sem limite).
    FixedStepClock(double step, int maxSteps) : m_step(step), m_maxSteps(maxSteps) {}

    // Soma o `dt` do quadro e devolve quantos passos fixos rodar agora. Quadro lento demais (mais que maxSteps
    // passos): cada passo a mais deixaria o próximo quadro ainda mais lento, com ainda mais passos. O tempo que
    // sobra é descartado e sai também do `dt` (o Update recebe menos): o jogo inteiro fica mais lento junto, em vez
    // de a física ficar para trás dos scripts.
    int Advance(double& dt) {
        m_accumulator += dt;
        const int whole = static_cast<int>(std::floor(m_accumulator / m_step));
        m_accumulator = std::max(m_accumulator - whole * m_step, 0.0);  // os rodados e os descartados
        if (m_maxSteps > 0 && whole > m_maxSteps) {
            dt = std::max(dt - (whole - m_maxSteps) * m_step, 0.0);
            return m_maxSteps;
        }
        return whole;
    }

    // Quanto do próximo passo já passou (0 a 1): para interpolar o que o passo fixo mexe.
    float Alpha() const { return static_cast<float>(std::clamp(m_accumulator / m_step, 0.0, 1.0)); }
    double Step() const { return m_step; }

private:
    double m_step;
    int m_maxSteps;
    double m_accumulator = 0.0;
};

} // namespace motor

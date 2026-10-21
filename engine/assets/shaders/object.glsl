// Dados por objeto e desenho em lote (instancing, Fase 23). O Renderer grava um Object por item da cena
// (ObjectData no Renderer.cpp: muda um, muda o outro) e, em cada passada, a lista de índices dos desenhos: um draw
// instanciado desenha uDrawIndices[uDrawBase .. uDrawBase + instâncias - 1].
struct Object {
    mat4 model;
    mat4 normal;  // inversa transposta da parte 3x3 do model, nas 3 primeiras colunas
};

layout(std430, binding = 4) readonly buffer Objects {
    Object uObjects[];
};

layout(std430, binding = 5) readonly buffer DrawIndices {
    uint uDrawIndices[];
};

uniform int uDrawBase;

Object CurrentObject() {
    return uObjects[uDrawIndices[uDrawBase + gl_InstanceID]];
}

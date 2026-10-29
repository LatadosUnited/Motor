// Dados por objeto e desenho em lote (instancing, Fase 23; pela GPU, Fase 28). O Renderer grava um Object por item
// da cena (ObjectData no Renderer.cpp: muda um, muda o outro) e, em cada passada, a lista de índices dos objetos.
// Cada instância recebe o seu índice pelo atributo 5 (um por instância): o comando de desenho indireto aponta para a
// parte dele da lista com o baseInstance, que o gl_InstanceID não inclui.
struct Object {
    mat4 model;
    mat4 normal;  // inversa transposta da parte 3x3 do model, nas 3 primeiras colunas
};

layout(std430, binding = 4) readonly buffer Objects {
    Object uObjects[];
};

layout(location = 5) in uint aObject;

Object CurrentObject() {
    return uObjects[aObject];
}

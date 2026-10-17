/* Implementação do miniaudio (e do stb_vorbis, que decodifica .ogg) num arquivo C só deles, compilado sem os
   avisos do nosso código (CMakeLists.txt da camada). O resto da camada só inclui o header miniaudio.h. */

#define MA_NO_ENCODING  /* só tocamos arquivos; gravar não é usado */

/* O stb_vorbis precisa ser declarado antes da implementação do miniaudio, que o detecta e usa para .ogg. */
#define STB_VORBIS_HEADER_ONLY
#include "extras/stb_vorbis.c"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#undef STB_VORBIS_HEADER_ONLY
#include "extras/stb_vorbis.c"

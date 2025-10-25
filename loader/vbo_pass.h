#pragma once
#include <vitaGL.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float x, y;      // position
    float r, g, b, a;// color (float, 0..1)
    float u, v;      // texcoord
} VBO_Vtx;

typedef struct {
    GLuint prog;
    GLint  u_proj;
    GLint  u_sampler;

    GLint  a_pos;
    GLint  a_col;
    GLint  a_uv;

    GLuint vbo;
    GLuint ibo;
    GLuint tex;

    float  proj[16];
} VBO_Pass;

/* Init with existing GL context (after vglInit*). 
   If 'texture_rgba8888' is NULL, pass 0 for tex_w/tex_h and it will create a 1x1 white tex. */
int  vbo_pass_init(VBO_Pass* p,
                   const void* texture_rgba8888, int tex_w, int tex_h,
                   int screen_w, int screen_h);

void vbo_pass_resize(VBO_Pass* p, int screen_w, int screen_h); // rebuild ortho
void vbo_pass_draw(VBO_Pass* p);                                // draws one textured quad
void vbo_pass_shutdown(VBO_Pass* p);

#ifdef __cplusplus
}
#endif

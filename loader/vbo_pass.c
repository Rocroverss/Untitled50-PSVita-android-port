#include "vbo_pass.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ---------- GLSL ES 2.0 (no semantics) ---------- */
static const char* VS_SRC =
"attribute vec2 a_position;\n"
"attribute vec4 a_color;\n"
"attribute vec2 a_texCoord;\n"
"uniform   mat4 u_projection;\n"
"varying   vec2 v_texCoord;\n"
"varying   vec4 v_color;\n"
"void main(){\n"
"    v_texCoord = a_texCoord;\n"
"    v_color    = a_color;\n"
"    gl_Position = u_projection * vec4(a_position, 0.0, 1.0);\n"
"    gl_PointSize = 1.0;\n"
"}\n";

static const char* FS_SRC =
"precision mediump float;\n"
"uniform sampler2D u_texture;\n"
"varying vec2 v_texCoord;\n"
"varying vec4 v_color;\n"
"void main(){\n"
"    vec4 abgr = texture2D(u_texture, v_texCoord);\n"
"    vec4 color = abgr;\n"
"    float t = color.r; color.r = color.b; color.b = t; /* ABGR->RGBA swap */\n"
"    color.a = 1.0;                                     /* force opaque   */\n"
"    gl_FragColor = color * v_color;\n"
"}\n";

/* ---------- helpers ---------- */
static void mat4_ortho(float* m, float l, float r, float b, float t, float n, float f) {
    // column-major GL matrix
    memset(m, 0, sizeof(float) * 16);
    m[0]  =  2.0f / (r - l);
    m[5]  =  2.0f / (t - b);
    m[10] = -2.0f / (f - n);
    m[12] = -(r + l) / (r - l);
    m[13] = -(t + b) / (t - b);
    m[14] = -(f + n) / (f - n);
    m[15] =  1.0f;
}

static GLuint compile(GLenum type, const char* src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    GLint ok = GL_FALSE;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        GLsizei n=0;
        glGetShaderInfoLog(sh, sizeof(log), &n, log);
        sceClibPrintf("shader compile error: %s\n", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

static GLuint link(GLuint vs, GLuint fs,
                   const char* pos, const char* col, const char* uv) {
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);

    // bind explicit locations BEFORE link (keeps things deterministic)
    glBindAttribLocation(prog, 0, pos);
    glBindAttribLocation(prog, 1, col);
    glBindAttribLocation(prog, 2, uv);

    glLinkProgram(prog);
    GLint ok = GL_FALSE;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        GLsizei n=0;
        glGetProgramInfoLog(prog, sizeof(log), &n, log);
        sceClibPrintf("program link error: %s\n", log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

static GLuint make_texture_rgba8888(const void* pixels, int w, int h) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);

    if (!pixels || w<=0 || h<=0) {
        // 1x1 white
        uint32_t one = 0xFFFFFFFFu;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, &one);
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    }
    return tex;
}

/* ---------- API ---------- */
int vbo_pass_init(VBO_Pass* p,
                  const void* texture_rgba8888, int tex_w, int tex_h,
                  int screen_w, int screen_h)
{
    memset(p, 0, sizeof(*p));

    GLuint vs = compile(GL_VERTEX_SHADER, VS_SRC);
    if (!vs) return 0;
    GLuint fs = compile(GL_FRAGMENT_SHADER, FS_SRC);
    if (!fs) { glDeleteShader(vs); return 0; }

    p->prog = link(vs, fs, "a_position", "a_color", "a_texCoord");
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!p->prog) return 0;

    p->u_proj   = glGetUniformLocation(p->prog, "u_projection");
    p->u_sampler= glGetUniformLocation(p->prog, "u_texture");
    p->a_pos    = glGetAttribLocation (p->prog, "a_position"); // expect 0
    p->a_col    = glGetAttribLocation (p->prog, "a_color");    // expect 1
    p->a_uv     = glGetAttribLocation (p->prog, "a_texCoord"); // expect 2

    // Projection (origin at top-left, y down)
    vbo_pass_resize(p, screen_w, screen_h);

    // Geometry: a screen-space quad spanning 256x256 at (x=100,y=100)
    const float x = 100.f, y = 100.f, w = 256.f, h = 256.f;
    VBO_Vtx verts[4] = {
        { x+0,   y+0,   1,1,1,1,   0,0 },
        { x+w,   y+0,   1,1,1,1,   1,0 },
        { x+w,   y+h,   1,1,1,1,   1,1 },
        { x+0,   y+h,   1,1,1,1,   0,1 },
    };
    GLushort idx[6] = { 0,1,2,  0,2,3 };

    glGenBuffers(1, &p->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, p->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

    glGenBuffers(1, &p->ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, p->ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);

    p->tex = make_texture_rgba8888(texture_rgba8888, tex_w, tex_h);
    return 1;
}

void vbo_pass_resize(VBO_Pass* p, int screen_w, int screen_h) {
    // Ortho with (0,0) in top-left, (w,h) bottom-right
    mat4_ortho(p->proj, 0.f, (float)screen_w, (float)screen_h, 0.f, -1.f, 1.f);
}

void vbo_pass_draw(VBO_Pass* p) {
    glUseProgram(p->prog);

    // Set uniforms
    glUniformMatrix4fv(p->u_proj, 1, GL_FALSE, p->proj);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, p->tex);
    glUniform1i(p->u_sampler, 0);

    // Bind VBO/IBO and attribute pointers
    glBindBuffer(GL_ARRAY_BUFFER, p->vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, p->ibo);

    const GLsizei stride = sizeof(VBO_Vtx);
    const GLvoid* off_pos = (const GLvoid*)offsetof(VBO_Vtx, x);
    const GLvoid* off_col = (const GLvoid*)offsetof(VBO_Vtx, r);
    const GLvoid* off_uv  = (const GLvoid*)offsetof(VBO_Vtx, u);

    glEnableVertexAttribArray((GLuint)p->a_pos);
    glVertexAttribPointer((GLuint)p->a_pos, 2, GL_FLOAT, GL_FALSE, stride, off_pos);

    glEnableVertexAttribArray((GLuint)p->a_col);
    glVertexAttribPointer((GLuint)p->a_col, 4, GL_FLOAT, GL_FALSE, stride, off_col);

    glEnableVertexAttribArray((GLuint)p->a_uv);
    glVertexAttribPointer((GLuint)p->a_uv, 2, GL_FLOAT, GL_FALSE, stride, off_uv);

    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);

    glDisableVertexAttribArray((GLuint)p->a_pos);
    glDisableVertexAttribArray((GLuint)p->a_col);
    glDisableVertexAttribArray((GLuint)p->a_uv);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void vbo_pass_shutdown(VBO_Pass* p) {
    if (p->tex) glDeleteTextures(1, &p->tex), p->tex=0;
    if (p->vbo) glDeleteBuffers (1, &p->vbo), p->vbo=0;
    if (p->ibo) glDeleteBuffers (1, &p->ibo), p->ibo=0;
    if (p->prog) glDeleteProgram(p->prog), p->prog=0;
}

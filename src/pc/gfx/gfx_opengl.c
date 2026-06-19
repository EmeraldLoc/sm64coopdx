#include <stdint.h>
#include <stdbool.h>

#ifndef _LANGUAGE_C
# define _LANGUAGE_C
#endif
#include <PR/gbi.h>

#ifdef __MINGW32__
# define FOR_WINDOWS 1
#else
# define FOR_WINDOWS 0
#endif

#if FOR_WINDOWS || defined(OSX_BUILD)
# define GLEW_STATIC
# include <GL/glew.h>
#endif

#define GL_GLEXT_PROTOTYPES 1

#include <SDL2/SDL.h>
#ifdef USE_GLES
#include <SDL2/SDL_opengles2.h>
#else
#include <SDL2/SDL_opengl.h>
#endif

#include "../platform.h"
#include "../configfile.h"
#include "gfx_cc.h"
#include "gfx_rendering_api.h"
#include "gfx_shader.h"
#include "gfx_pc.h"
#include "gfx_opengl.h"
#include "pc/lua/smlua.h"
#include "game/rendering_graph_node.h"

#define TEX_CACHE_STEP 512

struct GLTexture {
    GLuint gltex;
    GLfloat size[2];
    bool filter;
};

static struct ShaderProgram shader_program_pool[MAX_FRAME_PASSES][CC_MAX_SHADERS];
static uint8_t shader_program_pool_size[MAX_FRAME_PASSES] = { 0 };
static uint8_t shader_program_pool_index[MAX_FRAME_PASSES] = { 0 };

static struct ShaderProgram post_process_shader_program_pool[MAX_FRAME_PASSES];

static GLuint opengl_vbo;
static GLuint opengl_vao;

static int tex_cache_size = 0;
static int num_textures = 0;
static struct GLTexture *tex_cache = NULL;

static struct ShaderProgram *opengl_prg = NULL;
static struct GLTexture *opengl_tex[2];
static int opengl_curtex = 0;


static bool gfx_opengl_z_is_from_0_to_1(void) {
    return false;
}

static void gfx_opengl_vertex_array_set_attribs(struct ShaderProgram *prg) {
    size_t num_floats = prg->num_floats;
    size_t pos = 0;

    for (int i = 0; i < MAX_SHADER_INPUTS; i++) {
        glDisableVertexAttribArray(i);
    }

    for (int i = 0; i < prg->num_attribs; i++) {
        glEnableVertexAttribArray(prg->attrib_locations[i]);
        glVertexAttribPointer(prg->attrib_locations[i], prg->attrib_sizes[i], GL_FLOAT, GL_FALSE, num_floats * sizeof(float), (void *) (pos * sizeof(float)));
        pos += prg->attrib_sizes[i];
    }
}

static inline void gfx_opengl_set_shader_uniforms(void) {
    gfx_set_builtin_uniforms();
    smlua_call_event_hooks(HOOK_ON_SET_SHADER_UNIFORMS);
}

static inline void gfx_opengl_set_texture_uniforms(struct ShaderProgram *prg, const int tile) {
    if (!prg) return;
    if (opengl_tex[tile]) {
        glUniform2f(prg->uniform_locations[tile * 2 + 0], opengl_tex[tile]->size[0], opengl_tex[tile]->size[1]);
        glUniform1i(prg->uniform_locations[tile * 2 + 1], opengl_tex[tile]->filter);
    }
}

static void gfx_opengl_unload_shader(struct ShaderProgram *old_prg) {
    if (old_prg != NULL) {
        for (int i = 0; i < old_prg->num_attribs; i++) {
            glDisableVertexAttribArray(old_prg->attrib_locations[i]);
        }

        if (old_prg == opengl_prg) {
            opengl_prg = NULL;
        }
    } else {
        opengl_prg = NULL;
    }
}

static void gfx_opengl_load_shader(struct ShaderProgram *new_prg) {
    opengl_prg = new_prg;
    glUseProgram(new_prg->opengl_program_id);
    gfx_opengl_vertex_array_set_attribs(new_prg);
    gfx_opengl_set_shader_uniforms();
    gfx_opengl_set_texture_uniforms(new_prg, 0);
    gfx_opengl_set_texture_uniforms(new_prg, 1);
}

static void gfx_opengl_remove_shaders(void) {
    for (int i = 0; i < MAX_FRAME_PASSES; i++) {
        for (int j = 0; j < CC_MAX_SHADERS; j++) {
            gfx_opengl_unload_shader(&shader_program_pool[i][j]);
            gfx_destroy_shader(shader_program_pool[i][j].vertexShader);
            gfx_destroy_shader(shader_program_pool[i][j].fragmentShader);
            memset(&shader_program_pool[i][j], 0, sizeof(shader_program_pool[i][j]));
        }

        gfx_opengl_unload_shader(&post_process_shader_program_pool[i]);
        gfx_destroy_shader(post_process_shader_program_pool[i].vertexShader);
        gfx_destroy_shader(post_process_shader_program_pool[i].fragmentShader);
        memset(&post_process_shader_program_pool[i], 0, sizeof(post_process_shader_program_pool[i]));

        shader_program_pool_index[i] = 0;
        shader_program_pool_size[i] = 0;
    }
}

static struct ShaderProgram *gfx_opengl_create_and_load_new_shader(struct ColorCombiner *cc) {
    struct CCFeatures ccf = { 0 };
    gfx_cc_get_features(cc, &ccf);

    bool opt_alpha = cc->cm.use_alpha;
    bool opt_light_map = cc->cm.light_map;
    bool world_geometry = cc->cm.world_geometry;
    bool opt_dither = cc->cm.use_dither;

    char *vs_buf = gfx_generate_default_vertex_shader_from_cc(cc);
    char *fs_buf = gfx_generate_default_fragment_shader_from_cc(cc);

    /*puts("Vertex shader:");
    puts(vs_buf);
    puts("Fragment shader:");
    puts(fs_buf);
    puts("End");*/

    char *vsShaderCode = strdup(vs_buf);
    if (!vsShaderCode) {
        sys_fatal("Failed to allocate vertex shader, ran out of memory!");
    }
    char *fsShaderCode = strdup(fs_buf);
    if (!fsShaderCode) {
        sys_fatal("Failed to allocate fragment shader, ran out of memory!");
    }

    bool usingCustomVertexShader = false;
    bool usingCustomFragmentShader = false;

    int framePassIndex = gCurrentFramePassIndex + 1;

    smlua_call_event_hooks(HOOK_ON_VERTEX_SHADER_CREATE, cc, shader_program_pool_index[framePassIndex], (const char **)&vsShaderCode);
    smlua_call_event_hooks(HOOK_ON_FRAGMENT_SHADER_CREATE, cc, shader_program_pool_index[framePassIndex], (const char **)&fsShaderCode);

    if (strcmp(vsShaderCode, vs_buf) != 0) { usingCustomVertexShader = true; }
    if (strcmp(fsShaderCode, fs_buf) != 0) { usingCustomFragmentShader = true; }

    struct Shader *vertexShader = calloc(1, sizeof(struct Shader));
    if (!vertexShader) {
        sys_fatal("Failed to allocate vertex shader, ran out of memory!");
    }
    vertexShader->stage = GLSLANG_STAGE_VERTEX;

    // !! memory leak? Shader TODO: Verify lua handles it's string memory. It may need to be freed
    gfx_sanitize_vertex_shader(vertexShader, gShaderInputs, gShaderBindings, &vsShaderCode);

    struct Shader *fragmentShader = calloc(1, sizeof(struct Shader));
    if (!fragmentShader) {
        sys_fatal("Failed to allocate fragment shader, ran out of memory!");
    }
    fragmentShader->stage = GLSLANG_STAGE_FRAGMENT;

    // !! memory leak? Shader TODO: Verify lua handles it's string memory. It may need to be freed
    gfx_sanitize_fragment_shader(fragmentShader, vertexShader->shaderOutputs, gShaderBindings, &fsShaderCode);

    if (usingCustomVertexShader) {
        // make sure it compiles with glslang first
        if (!gfx_compile_shader_to_spirv(GLSLANG_STAGE_VERTEX, vsShaderCode, vertexShader)) {
            LOG_ERROR("Failed to compile vertex shader!");
            usingCustomVertexShader = false;
            free(vsShaderCode);
            vsShaderCode = (char*)vs_buf;
        }
    }

    if (usingCustomFragmentShader) {
        // make sure it compiles with glslang first
        if (!gfx_compile_shader_to_spirv(GLSLANG_STAGE_FRAGMENT, fsShaderCode, fragmentShader)) {
            LOG_ERROR("Failed to compile fragment shader!");
            usingCustomFragmentShader = false;
            free(fsShaderCode);
            fsShaderCode = (char*)fs_buf;
        }
    }

    const GLchar *sources[2] = { vsShaderCode, fsShaderCode };
    GLint lengths[2] = { strlen(vsShaderCode), strlen(fsShaderCode) };
    GLint success;

    GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_shader, 1, &sources[0], &lengths[0]);
    glCompileShader(vertex_shader);
    glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLint max_length = 0;
        glGetShaderiv(vertex_shader, GL_INFO_LOG_LENGTH, &max_length);
        char error_log[1024];
        glGetShaderInfoLog(vertex_shader, max_length, &max_length, &error_log[0]);
        if (!usingCustomVertexShader) {
            fprintf(stderr, "Vertex shader compilation failed\n");
            fprintf(stderr, "%s\n", &error_log[0]);
            sys_fatal("vertex shader compilation failed (see terminal)");
        } else {
            LOG_LUA_LINE("Vertex Shader: %s", error_log);
        }
        usingCustomVertexShader = false;
        sources[0] = vs_buf;
        lengths[0] = strlen(vs_buf);
        glShaderSource(vertex_shader, 1, &sources[0], &lengths[0]);
        glCompileShader(vertex_shader);
        glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            fprintf(stderr, "Vertex shader compilation failed\n");
            glGetShaderInfoLog(vertex_shader, max_length, &max_length, &error_log[0]);
            fprintf(stderr, "%s\n", &error_log[0]);
            sys_fatal("vertex shader compilation failed (see terminal)");
        }
    }

    GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment_shader, 1, &sources[1], &lengths[1]);
    glCompileShader(fragment_shader);
    glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLint max_length = 0;
        glGetShaderiv(fragment_shader, GL_INFO_LOG_LENGTH, &max_length);
        char error_log[1024];
        glGetShaderInfoLog(fragment_shader, max_length, &max_length, &error_log[0]);
        if (!usingCustomFragmentShader) {
            fprintf(stderr, "Fragment shader compilation failed\n");
            fprintf(stderr, "%s\n", &error_log[0]);
            sys_fatal("fragment shader compilation failed (see terminal)");
        } else {
            LOG_LUA_LINE("Fragment Shader: %s", &error_log[0]);
        }
        usingCustomFragmentShader = false;
        sources[1] = fs_buf;
        lengths[1] = strlen(fs_buf);
        glShaderSource(fragment_shader, 1, &sources[1], &lengths[1]);
        glCompileShader(fragment_shader);
        glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            fprintf(stderr, "Fragment shader compilation failed\n");
            glGetShaderInfoLog(fragment_shader, max_length, &max_length, &error_log[0]);
            fprintf(stderr, "%s\n", &error_log[0]);
            sys_fatal("fragment shader compilation failed (see terminal)");
        }
    }

    GLuint shader_program = glCreateProgram();
    glAttachShader(shader_program, vertex_shader);
    glAttachShader(shader_program, fragment_shader);
    glLinkProgram(shader_program);

    struct ShaderProgram *prg = &shader_program_pool[framePassIndex][shader_program_pool_index[framePassIndex]];
    shader_program_pool_index[framePassIndex] = (shader_program_pool_index[framePassIndex] + 1) % CC_MAX_SHADERS;
    if (shader_program_pool_size[framePassIndex] < CC_MAX_SHADERS) { shader_program_pool_size[framePassIndex]++; }

    size_t cnt = 0;
    size_t num_floats = 0;

    for (int i = 0; i < MAX_SHADER_INPUTS; i++) {
        if (gShaderInputs[i].size == 0) { continue; }
        prg->attrib_locations[i] = gShaderInputs[i].location;
        prg->attrib_sizes[i] = gShaderInputs[i].size;
        num_floats += gShaderInputs[i].size;
        cnt++;
    }

    prg->hash = cc->hash;
    prg->opengl_program_id = shader_program;
    prg->num_inputs = ccf.num_inputs;
    prg->used_textures[0] = ccf.used_textures[0];
    prg->used_textures[1] = ccf.used_textures[1];
    prg->num_floats = num_floats;
    prg->num_attribs = cnt;

    glUseProgram(shader_program);

    for (int t = 0; t < 2; t++) {
        char name[16];
        sprintf(name, "uTex%d", t);
        GLint sampler_location = glGetUniformLocation(shader_program, name);
        sprintf(name, "uTex%dSize", t);
        prg->uniform_locations[t * 2] = glGetUniformLocation(shader_program, name);
        sprintf(name, "uTex%dFilter", t);
        prg->uniform_locations[t * 2 + 1] = glGetUniformLocation(shader_program, name);
        glUniform1i(sampler_location, t);
    }

    if ((opt_alpha && opt_dither) || ccf.do_noise) {
        prg->used_noise = true;
    } else {
        prg->used_noise = false;
    }

    prg->used_lightmap = opt_light_map;
    prg->world_geometry = world_geometry;

    GLint passTexLoc = glGetUniformLocation(shader_program, "uPassTex");
    if (passTexLoc != -1) {
        glUniform1i(passTexLoc, 10);
    }

    prg->vertexShader = vertexShader;
    prg->fragmentShader = fragmentShader;

    gfx_opengl_load_shader(prg);

    return prg;
}

static struct ShaderProgram *gfx_opengl_create_or_load_post_process_shader(void) {
    int framePassIndex = gCurrentFramePassIndex + 1;
    // if a shader already exists, use that instead
    if (post_process_shader_program_pool[framePassIndex].opengl_program_id != 0) {
        gfx_opengl_load_shader(&post_process_shader_program_pool[framePassIndex]);
        return &post_process_shader_program_pool[framePassIndex];
    }

    char *vsShaderCode = (char*)gDefaultPostProcessVertexShader;
    char *fsShaderCode = (char*)gDefaultPostProcessFragmentShader;

    // let lua override the shader
    smlua_call_event_hooks(HOOK_ON_POST_PROCESS_VERTEX_SHADER_CREATE, (const char **)&vsShaderCode);
    smlua_call_event_hooks(HOOK_ON_POST_PROCESS_FRAGMENT_SHADER_CREATE, (const char **)&fsShaderCode);

    bool usingCustomVertexShader = (strcmp(vsShaderCode, gDefaultPostProcessVertexShader) != 0);
    bool usingCustomFragmentShader = (strcmp(fsShaderCode, gDefaultPostProcessFragmentShader) != 0);

    struct Shader *vertexShader = calloc(1, sizeof(struct Shader));
    if (!vertexShader) {
        sys_fatal("Failed to allocate vertex shader, ran out of memory!");
    }
    vertexShader->stage = GLSLANG_STAGE_VERTEX;

    // !! memory leak? Shader TODO: Verify lua handles it's string memory. It may need to be freed
    gfx_sanitize_vertex_shader(vertexShader, gPostProcessShaderInputs, gPostProcessShaderBindings, &vsShaderCode);

    struct Shader *fragmentShader = calloc(1, sizeof(struct Shader));
    if (!fragmentShader) {
        sys_fatal("Failed to allocate fragment shader, ran out of memory!");
    }
    fragmentShader->stage = GLSLANG_STAGE_FRAGMENT;

    // !! memory leak? Shader TODO: Verify lua handles it's string memory. It may need to be freed
    gfx_sanitize_fragment_shader(fragmentShader, vertexShader->shaderOutputs, gPostProcessShaderBindings, &fsShaderCode);

    if (usingCustomVertexShader) {
        // make sure it compiles with glslang first
        if (!gfx_compile_shader_to_spirv(GLSLANG_STAGE_VERTEX, vsShaderCode, vertexShader)) {
            LOG_ERROR("Failed to compile post process vertex shader!");
            usingCustomVertexShader = false;
            vsShaderCode = (char*)gDefaultPostProcessVertexShader;
        }
    }

    if (usingCustomFragmentShader) {
        // make sure it compiles with glslang first
        if (!gfx_compile_shader_to_spirv(GLSLANG_STAGE_FRAGMENT, fsShaderCode, fragmentShader)) {
            LOG_ERROR("Failed to compile post process fragment shader!");
            usingCustomFragmentShader = false;
            fsShaderCode = (char*)gDefaultPostProcessFragmentShader;
        }
    }

    const GLchar *sources[2] = { vsShaderCode, fsShaderCode };
    GLint lengths[2] = { strlen(vsShaderCode), strlen(fsShaderCode) };
    GLint success;

    GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_shader, 1, &sources[0], &lengths[0]);
    glCompileShader(vertex_shader);
    glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLint max_length = 0;
        glGetShaderiv(vertex_shader, GL_INFO_LOG_LENGTH, &max_length);
        char error_log[1024];
        glGetShaderInfoLog(vertex_shader, max_length, &max_length, &error_log[0]);
        if (!usingCustomVertexShader) {
            fprintf(stderr, "Vertex shader compilation failed\n");
            fprintf(stderr, "%s\n", &error_log[0]);
            sys_fatal("vertex shader compilation failed (see terminal)");
        } else {
            LOG_LUA_LINE("Vertex Shader: %s", error_log);
        }
        usingCustomVertexShader = false;
        sources[0] = gDefaultPostProcessVertexShader;
        lengths[0] = strlen(gDefaultPostProcessVertexShader);
        glShaderSource(vertex_shader, 1, &sources[0], &lengths[0]);
        glCompileShader(vertex_shader);
        glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            fprintf(stderr, "Vertex shader compilation failed\n");
            glGetShaderInfoLog(vertex_shader, max_length, &max_length, &error_log[0]);
            fprintf(stderr, "%s\n", &error_log[0]);
            sys_fatal("vertex shader compilation failed (see terminal)");
        }
    }

    GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment_shader, 1, &sources[1], &lengths[1]);
    glCompileShader(fragment_shader);
    glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLint max_length = 0;
        glGetShaderiv(fragment_shader, GL_INFO_LOG_LENGTH, &max_length);
        char error_log[1024];
        glGetShaderInfoLog(fragment_shader, max_length, &max_length, &error_log[0]);
        if (!usingCustomFragmentShader) {
            fprintf(stderr, "Fragment shader compilation failed\n");
            fprintf(stderr, "%s\n", &error_log[0]);
            sys_fatal("fragment shader compilation failed (see terminal)");
        } else {
            LOG_LUA_LINE("Fragment Shader: %s", &error_log[0]);
        }
        usingCustomFragmentShader = false;
        sources[1] = gDefaultPostProcessFragmentShader;
        lengths[1] = strlen(gDefaultPostProcessFragmentShader);
        glShaderSource(fragment_shader, 1, &sources[1], &lengths[1]);
        glCompileShader(fragment_shader);
        glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            fprintf(stderr, "Fragment shader compilation failed\n");
            glGetShaderInfoLog(fragment_shader, max_length, &max_length, &error_log[0]);
            fprintf(stderr, "%s\n", &error_log[0]);
            sys_fatal("fragment shader compilation failed (see terminal)");
        }
    }

    GLuint shader_program = glCreateProgram();
    glAttachShader(shader_program, vertex_shader);
    glAttachShader(shader_program, fragment_shader);
    glLinkProgram(shader_program);

    size_t cnt = 0;
    size_t num_floats = 0;

    struct ShaderProgram *prg = &post_process_shader_program_pool[framePassIndex];

    for (int i = 0; i < MAX_SHADER_INPUTS; i++) {
        if (gPostProcessShaderInputs[i].size == 0) continue;
        prg->attrib_locations[i] = gPostProcessShaderInputs[i].location;
        prg->attrib_sizes[i] = gPostProcessShaderInputs[i].size;
        num_floats += gPostProcessShaderInputs[i].size;
        cnt++;
    }

    prg->hash = framePassIndex;
    prg->opengl_program_id = shader_program;
    prg->num_floats = num_floats;
    prg->num_attribs = cnt;

    prg->vertexShader = vertexShader;
    prg->fragmentShader = fragmentShader;

    gfx_opengl_load_shader(prg);

    for (int t = 0; t < 2; t++) {
        char name[16];
        sprintf(name, "uTex%d", t);
        GLint sampler_location = glGetUniformLocation(shader_program, name);
        sprintf(name, "uTex%dSize", t);
        prg->uniform_locations[t * 2] = glGetUniformLocation(shader_program, name);
        sprintf(name, "uTex%dFilter", t);
        prg->uniform_locations[t * 2 + 1] = glGetUniformLocation(shader_program, name);
        glUniform1i(sampler_location, t);
    }

    GLint passTexLoc = glGetUniformLocation(shader_program, "uPassTex");
    if (passTexLoc != -1) {
        glUniform1i(passTexLoc, 10);
    }

    return prg;
}

static struct ShaderProgram *gfx_opengl_lookup_shader(struct ColorCombiner *cc) {
    int framePassIndex = gCurrentFramePassIndex + 1;
    if (framePassIndex == 0) { return NULL; }
    for (size_t i = 0; i < shader_program_pool_size[framePassIndex]; i++) {
        if (shader_program_pool[framePassIndex][i].hash == cc->hash) {
             return &shader_program_pool[framePassIndex][i];
        }
    }
    return NULL;
}

static struct ShaderProgram* gfx_opengl_lookup_shader_using_index(uint8_t shaderIndex, uint8_t framePassIndex) {
    framePassIndex++;
    if (shaderIndex >= shader_program_pool_size[framePassIndex]) return NULL;
    return &shader_program_pool[framePassIndex][shaderIndex];
}

static void gfx_opengl_shader_get_info(struct ShaderProgram *prg, uint8_t *num_inputs, bool used_textures[2]) {
    *num_inputs = prg->num_inputs;
    used_textures[0] = prg->used_textures[0];
    used_textures[1] = prg->used_textures[1];
}

static void gfx_opengl_create_framebuffer(struct FramePass *framePass) {
    glGenFramebuffers(1, &framePass->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, framePass->fbo);

    glGenTextures(1, &framePass->passTexture);
    glBindTexture(GL_TEXTURE_2D, framePass->passTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, framePass->width, framePass->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, framePass->passTexture, 0);

    // create depth buffer
    glGenRenderbuffers(1, &framePass->depthBuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, framePass->depthBuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, framePass->width, framePass->height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, framePass->depthBuffer);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        LOG_ERROR("Framebuffer is not complete!");
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void gfx_opengl_delete_framebuffer(struct FramePass *framePass) {
    if (framePass->fbo > 0) { glDeleteFramebuffers(1, &framePass->fbo); }
    if (framePass->depthBuffer > 0) { glDeleteRenderbuffers(1, &framePass->depthBuffer); }
    if (framePass->passTexture > 0) { glDeleteTextures(1, &framePass->passTexture); }
}

static void gfx_opengl_set_framebuffer(struct FramePass *framePass) {
    glBindFramebuffer(GL_FRAMEBUFFER, framePass->fbo);
    glViewport(0, 0, framePass->width, framePass->height);
}

static void gfx_opengl_reset_framebuffer(void) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    u32 windowWidth, windowHeight;
    gfx_get_dimensions(&windowWidth, &windowHeight);
    glViewport(0, 0, windowWidth, windowHeight);
}

static void gfx_opengl_set_uniform_for_shader(struct Shader *shader, const char* name, ShaderUniformType type, const void *data, uint32_t numElements) {
    if (!shader) { return; }
    for (int i = 0; i < MAX_SHADER_UNIFORMS; i++) {
        if (shader->shaderUniforms[i].size == 0) { break; }

        if (strcmp(shader->shaderUniforms[i].name, name) == 0) {
            GLint loc = shader->shaderUniforms[i].location;
            switch (type) {
                case SHADER_UNIFORM_TYPE_BOOL:  glUniform1iv(loc, numElements, (const GLint*)data); break;
                case SHADER_UNIFORM_TYPE_INT:   glUniform1iv(loc, numElements, (const GLint*)data); break;
                case SHADER_UNIFORM_TYPE_FLOAT: glUniform1fv(loc, numElements, (const GLfloat*)data); break;
                case SHADER_UNIFORM_TYPE_VEC2:  glUniform2fv(loc, numElements, (const GLfloat*)data); break;
                case SHADER_UNIFORM_TYPE_VEC3:  glUniform3fv(loc, numElements, (const GLfloat*)data); break;
                case SHADER_UNIFORM_TYPE_VEC4:  glUniform4fv(loc, numElements, (const GLfloat*)data); break;
                case SHADER_UNIFORM_TYPE_MAT4:  glUniformMatrix4fv(loc, numElements, GL_FALSE, (const GLfloat*)data); break;
            }
        }
    }
}

static void gfx_opengl_set_uniform(struct ShaderProgram *prg, const char *name, ShaderUniformType type, const void *data, uint32_t numElements) {
    if (!prg) {
        if (!opengl_prg) { return; }
        prg = opengl_prg;
    }
    gfx_opengl_set_uniform_for_shader(prg->vertexShader, name, type, data, numElements);
    gfx_opengl_set_uniform_for_shader(prg->fragmentShader, name, type, data, numElements);
}

static GLuint gfx_opengl_new_texture(void) {
    if (num_textures >= tex_cache_size) {
        tex_cache_size += TEX_CACHE_STEP;
        tex_cache = realloc(tex_cache, sizeof(struct GLTexture) * tex_cache_size);
        if (!tex_cache) sys_fatal("out of memory allocating texture cache");
        // invalidate these because they might be pointing to garbage now
        opengl_tex[0] = NULL;
        opengl_tex[1] = NULL;
    }
    glGenTextures(1, &tex_cache[num_textures].gltex);
    return num_textures++;
}

static void gfx_opengl_select_texture(int tile, GLuint texture_id) {
    opengl_tex[tile] = tex_cache + texture_id;
    opengl_curtex = tile;
    glActiveTexture(GL_TEXTURE0 + tile);
    glBindTexture(GL_TEXTURE_2D, opengl_tex[tile]->gltex);
    gfx_opengl_set_texture_uniforms(opengl_prg, tile);
}

static void gfx_opengl_bind_texture_raw(int tile, uint64_t texture_id) {
    glActiveTexture(GL_TEXTURE0 + tile);
    glBindTexture(GL_TEXTURE_2D, (GLuint)texture_id);
}

static void gfx_opengl_upload_texture(const uint8_t *rgba32_buf, int width, int height) {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba32_buf);
    opengl_tex[opengl_curtex]->size[0] = width;
    opengl_tex[opengl_curtex]->size[1] = height;
}

static uint32_t gfx_cm_to_opengl(uint32_t val) {
    if (val & G_TX_CLAMP) {
        return GL_CLAMP_TO_EDGE;
    }
    return (val & G_TX_MIRROR) ? GL_MIRRORED_REPEAT : GL_REPEAT;
}

static void gfx_opengl_set_sampler_parameters(int tile, bool linear_filter, uint32_t cms, uint32_t cmt) {
    const GLenum filter = linear_filter ? GL_LINEAR : GL_NEAREST;
    glActiveTexture(GL_TEXTURE0 + tile);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gfx_cm_to_opengl(cms));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gfx_cm_to_opengl(cmt));
    opengl_curtex = tile;
    if (opengl_tex[tile]) {
        opengl_tex[tile]->filter = linear_filter;
        gfx_opengl_set_texture_uniforms(opengl_prg, tile);
    }
}

static void gfx_opengl_set_depth_test(bool depth_test) {
    if (depth_test) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
}

static void gfx_opengl_set_depth_mask(bool z_upd) {
    glDepthMask(z_upd ? GL_TRUE : GL_FALSE);
}

static void gfx_opengl_set_zmode_decal(bool zmode_decal) {
    if (zmode_decal) {
        glPolygonOffset(-2, -2);
        glEnable(GL_POLYGON_OFFSET_FILL);
    } else {
        glPolygonOffset(0, 0);
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
}

static void gfx_opengl_set_viewport(int x, int y, int width, int height) {
    glViewport(x, y, width, height);
}

static void gfx_opengl_set_scissor(int x, int y, int width, int height) {
    glScissor(x, y, width, height);
}

static void gfx_opengl_set_use_alpha(bool use_alpha) {
    if (use_alpha) {
        glEnable(GL_BLEND);
    } else {
        glDisable(GL_BLEND);
    }
}

static void gfx_opengl_draw_triangles(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris) {
    //printf("flushing %d tris\n", buf_vbo_num_tris);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * buf_vbo_len, buf_vbo, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 3 * buf_vbo_num_tris);
}

static inline bool gl_get_version(int *major, int *minor, bool *is_es) {
    const char *vstr = (const char *)glGetString(GL_VERSION);
    if (!vstr || !vstr[0]) return false;

    if (!strncmp(vstr, "OpenGL ES ", 10)) {
        vstr += 10;
        *is_es = true;
    } else if (!strncmp(vstr, "OpenGL ES-CM ", 13)) {
        vstr += 13;
        *is_es = true;
    }

    return (sscanf(vstr, "%d.%d", major, minor) == 2);
}

static void gfx_opengl_init(void) {
#if FOR_WINDOWS || defined(OSX_BUILD)
    GLenum err;
    if ((err = glewInit()) != GLEW_OK)
        sys_fatal("could not init GLEW:\n%s", glewGetErrorString(err));
#endif

    tex_cache_size = TEX_CACHE_STEP;
    tex_cache = calloc(tex_cache_size, sizeof(struct GLTexture));
    if (!tex_cache) sys_fatal("out of memory allocating texture cache");

    // check GL version
    int vmajor = 0;
    int vminor = 0;
    bool is_es = false;
    gl_get_version(&vmajor, &vminor, &is_es);
    if (vmajor < 2 && vminor < 1 && !is_es)
        sys_fatal("OpenGL 2.1+ is required.\nReported version: %s%d.%d", is_es ? "ES" : "", vmajor, vminor);

    glGenBuffers(1, &opengl_vbo);

    glBindBuffer(GL_ARRAY_BUFFER, opengl_vbo);

    if (vmajor >= 3 && !is_es) {
        glGenVertexArrays(1, &opengl_vao);
        glBindVertexArray(opengl_vao);
    }

    glDepthFunc(GL_LEQUAL);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

static void gfx_opengl_on_resize(void) {
}

static void gfx_opengl_start_frame(void) {
    glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_TRUE); // Must be set to clear Z-buffer

    struct FramePass *framePass = gfx_get_current_frame_pass();
    glClearColor(
        framePass->clearColor[0] / 255.0f,
        framePass->clearColor[1] / 255.0f,
        framePass->clearColor[2] / 255.0f,
        framePass->clearColor[3] / 255.0f
    );

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
}

static void gfx_opengl_end_frame(void) {
}

static void gfx_opengl_finish_render(void) {
}

static const char* gfx_opengl_get_name(void) {
    return "OpenGL";
}

static void gfx_opengl_shutdown(void) {
}

struct GfxRenderingAPI gfx_opengl_api = {
    gfx_opengl_z_is_from_0_to_1,
    gfx_opengl_unload_shader,
    gfx_opengl_load_shader,
    gfx_opengl_remove_shaders,
    gfx_opengl_create_and_load_new_shader,
    gfx_opengl_create_or_load_post_process_shader,
    gfx_opengl_lookup_shader,
    gfx_opengl_lookup_shader_using_index,
    gfx_opengl_shader_get_info,
    gfx_opengl_create_framebuffer,
    gfx_opengl_delete_framebuffer,
    gfx_opengl_set_framebuffer,
    gfx_opengl_reset_framebuffer,
    gfx_opengl_set_uniform,
    gfx_opengl_new_texture,
    gfx_opengl_select_texture,
    gfx_opengl_bind_texture_raw,
    gfx_opengl_upload_texture,
    gfx_opengl_set_sampler_parameters,
    gfx_opengl_set_depth_test,
    gfx_opengl_set_depth_mask,
    gfx_opengl_set_zmode_decal,
    gfx_opengl_set_viewport,
    gfx_opengl_set_scissor,
    gfx_opengl_set_use_alpha,
    gfx_opengl_draw_triangles,
    gfx_opengl_init,
    gfx_opengl_on_resize,
    gfx_opengl_start_frame,
    gfx_opengl_end_frame,
    gfx_opengl_finish_render,
    gfx_opengl_get_name,
    gfx_opengl_shutdown
};

// This is a file to abstract a shader string into a standard structure. It's used to convert GLSL to
// HLSL as well as allow mods to modify uniforms and such by name

#pragma once

#define MAX_SHADER_CODE 65536
#define MAX_SHADER_VARIABLE_NAME 128
#define MAX_SHADER_TEXTURES 2
#define MAX_SHADER_INPUTS 64
#define MAX_SHADER_OUTPUTS 512
#define MAX_SHADER_UNIFORMS 8192
#define MAX_SHADER_BINDINGS 32

#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>
#include <spirv_cross/spirv_cross_c.h>

#include "gfx_cc.h"
#include "macros.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    u32 *words; // SPIR-V words
    int size; // number of words in SPIR-V shader
} SpirVShader;

typedef enum ShaderUniformType {
    SHADER_UNIFORM_TYPE_BOOL,
    SHADER_UNIFORM_TYPE_INT,
    SHADER_UNIFORM_TYPE_FLOAT,
    SHADER_UNIFORM_TYPE_VEC2,
    SHADER_UNIFORM_TYPE_VEC3,
    SHADER_UNIFORM_TYPE_VEC4,
    SHADER_UNIFORM_TYPE_MAT4
} ShaderUniformType;

struct ShaderBinding {
    char name[MAX_SHADER_VARIABLE_NAME];
    int binding;
};

struct ShaderUniform {
    char name[MAX_SHADER_VARIABLE_NAME];
    int location;
    int size;
};

struct ShaderInput {
    char name[MAX_SHADER_VARIABLE_NAME];
    int location;
    int size;
};

struct ShaderOutput {
    char name[MAX_SHADER_VARIABLE_NAME];
    int location;
};

struct Shader {
    glslang_stage_t stage;
    SpirVShader spirVShader;
    struct ShaderInput shaderInputs[MAX_SHADER_INPUTS];
    struct ShaderOutput shaderOutputs[MAX_SHADER_OUTPUTS];
    struct ShaderBinding shaderBindings[MAX_SHADER_BINDINGS];
    struct ShaderUniform shaderUniforms[MAX_SHADER_UNIFORMS];
    size_t uboTotalSize;
};

extern struct ShaderInput *gShaderInputs;
extern struct ShaderInput *gPostProcessShaderInputs;
extern struct ShaderBinding *gShaderBindings;
extern struct ShaderBinding *gPostProcessShaderBindings;

extern const char *gDefaultPostProcessVertexShader;
extern const char *gDefaultPostProcessFragmentShader;

char *gfx_generate_default_vertex_shader_from_cc(UNUSED struct ColorCombiner *cc);
char *gfx_generate_default_fragment_shader_from_cc(struct ColorCombiner *cc);
void gfx_init_shaders();
void gfx_sanitize_vertex_shader(struct Shader *shader, struct ShaderInput *referenceInputs, struct ShaderBinding *referenceBindings, char **shaderCode);
void gfx_sanitize_fragment_shader(struct Shader *shader, struct ShaderOutput *outputsFromVertexShader, struct ShaderBinding *referenceBindings, char **shaderCode);
struct Shader *gfx_create_shader(const char *shaderCode);
bool gfx_compile_shader_to_spirv(glslang_stage_t stage, const char *shaderCode, struct Shader *shader);
void gfx_convert_spirv_to_hlsl(char **shaderCode, struct Shader *shader);
void gfx_destroy_shader_contents(struct Shader *shader);
void gfx_destroy_shader(struct Shader *shader);

#ifdef __cplusplus
}
#endif
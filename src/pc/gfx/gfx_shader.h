// This is a file to abstract a shader string into a standard structure. It's used to convert GLSL to
// HLSL as well as allow mods to modify uniforms and such by name

#define MAX_SHADER_CODE 32768
#define MAX_SHADER_VARIABLE_NAME 128
#define MAX_SHADER_INPUT_NAME MAX_SHADER_VARIABLE_NAME
#define MAX_SHADER_OUTPUT_NAME MAX_SHADER_VARIABLE_NAME
#define MAX_SHADER_TEXTURES 2
#define MAX_SHADER_INPUTS 64
#define MAX_SHADER_OUTPUTS 64
#define MAX_SHADER_UNIFORMS 24
#define MAX_SHADER_BINDINGS 8

#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>
#include <spirv_cross/spirv_cross_c.h>

typedef struct SpirVBinary {
    u32 *words; // SPIR-V words
    int size; // number of words in SPIR-V binary
} SpirVBinary;

struct ShaderBinding {
    char name[MAX_SHADER_VARIABLE_NAME];
    int binding;
};

struct ShaderInput {
    char name[MAX_SHADER_INPUT_NAME];
    int location;
    int size;
};

struct ShaderOutput {
    char name[MAX_SHADER_OUTPUT_NAME];
    int location;
};

struct Shader {
    glslang_stage_t stage;
    SpirVBinary spirVBinary;
    struct ShaderInput shaderInputs[MAX_SHADER_INPUTS];
    struct ShaderOutput shaderOutputs[MAX_SHADER_OUTPUTS];
    struct ShaderBinding shaderBindings[MAX_SHADER_BINDINGS];
};

extern struct ShaderInput *gShaderInputs;
extern struct ShaderInput *gPostProcessShaderInputs;
extern struct ShaderBinding *gShaderBindings;
extern struct ShaderBinding *gPostProcessShaderBindings;

const char *gfx_get_default_post_process_vertex_shader();
const char *gfx_get_default_post_process_fragment_shader();
void gfx_init_shaders();
void gfx_sanitize_vertex_shader(struct Shader *shader, struct ShaderInput *referenceInputs, struct ShaderBinding *referenceBindings, char **shaderCode);
void gfx_sanitize_fragment_shader(struct Shader *shader, struct ShaderOutput *outputsFromVertexShader, struct ShaderBinding *referenceBindings, char **shaderCode);
struct Shader *gfx_create_shader(const char *shaderCode);
bool gfx_compile_shader_to_spirv(glslang_stage_t stage, const char *shaderCode, struct Shader *shader);
void gfx_convert_spirv_to_hlsl(struct Shader *shader);
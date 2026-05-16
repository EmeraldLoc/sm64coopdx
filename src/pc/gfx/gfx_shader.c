// This is a file to abstract a shader string into a standard structure. It's used to convert GLSL to
// HLSL as well as allow mods to modify uniforms and such by name

#include <stdlib.h>
#include <string.h>

#include <PR/ultratypes.h>

#include "gfx_shader.h"
#include "gfx_cc.h"

#include "pc/debuglog.h"

struct ShaderInput *gShaderInputs = NULL;
struct ShaderInput *gPostProcessShaderInputs = NULL;
struct ShaderBinding *gShaderBindings = NULL;
struct ShaderBinding *gPostProcessShaderBindings = NULL;

static int sShaderOutputCount = 0;

// should be strdupped
const char *gfx_get_default_post_process_vertex_shader() {
    return "#version 410 core\n"
    "in vec4 aVtxPos;\n"
    "out vec4 vVtxPos;\n" // exists for convenience of mods
    "in vec2 aTexCoord;\n"
    "out vec2 vTexCoord;\n"
    "void main() {\n"
    "    vVtxPos = aVtxPos;\n"
    "    vTexCoord = aTexCoord;\n"
    "    gl_Position = aVtxPos;\n"
    "}\n";
}

// should be strdupped
const char *gfx_get_default_post_process_fragment_shader() {
    return "#version 410 core\n"
    "uniform sampler2D uPassTex;\n"
    "in vec4 vVtxPos;\n" // exists for convenience of mods
    "in vec2 vTexCoord;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    fragColor = texture(uPassTex, vTexCoord);\n"
    "}\n";
}

static void gfx_init_shader_inputs() {
    gShaderInputs = calloc(MAX_SHADER_INPUTS, sizeof(struct ShaderInput));
    if (!gShaderInputs) {
        sys_fatal("Failed to allocate shader inputs, ran out of memory!");
    }

    gPostProcessShaderInputs = calloc(MAX_SHADER_INPUTS, sizeof(struct ShaderInput));
    if (!gPostProcessShaderInputs) {
        free(gShaderInputs);
        gShaderInputs = NULL;
        sys_fatal("Failed to allocate post process shader inputs, ran out of memory!");
    }

    int cnt = 0;

    snprintf(gShaderInputs[cnt].name, MAX_SHADER_INPUT_NAME, "aVtxPos");
    gShaderInputs[cnt].location = cnt;
    gShaderInputs[cnt].size = 4;
    cnt++;

    for (int t = 0; t < 2; t++) {
        snprintf(gShaderInputs[cnt].name, MAX_SHADER_INPUT_NAME, "aTexCoord%d", t);
        gShaderInputs[cnt].location = cnt;
        gShaderInputs[cnt].size = 2;
        ++cnt;
    }

    snprintf(gShaderInputs[cnt].name, MAX_SHADER_INPUT_NAME, "aFog");
    gShaderInputs[cnt].location = cnt;
    gShaderInputs[cnt].size = 4;
    ++cnt;

    snprintf(gShaderInputs[cnt].name, MAX_SHADER_INPUT_NAME, "aLightMap");
    gShaderInputs[cnt].location = cnt;
    gShaderInputs[cnt].size = 2;
    ++cnt;

    for (int i = 0; i < CC_MAX_INPUTS; i++) {
        snprintf(gShaderInputs[cnt].name, MAX_SHADER_INPUT_NAME, "aInput%d", i + 1);
        gShaderInputs[cnt].location = cnt;
        gShaderInputs[cnt].size = 4;
        ++cnt;
    }

    snprintf(gShaderInputs[cnt].name, MAX_SHADER_INPUT_NAME, "aNormal");
    gShaderInputs[cnt].location = cnt;
    gShaderInputs[cnt].size = 3;
    ++cnt;

    snprintf(gShaderInputs[cnt].name, MAX_SHADER_INPUT_NAME, "aBarycentric");
    gShaderInputs[cnt].location = cnt;
    gShaderInputs[cnt].size = 3;
    ++cnt;

    // post process shader inputs
    cnt = 0;

    snprintf(gPostProcessShaderInputs[cnt].name, MAX_SHADER_INPUT_NAME, "aVtxPos");
    gPostProcessShaderInputs[cnt].location = cnt;
    gPostProcessShaderInputs[cnt].size = 4;
    ++cnt;

    snprintf(gPostProcessShaderInputs[cnt].name, MAX_SHADER_INPUT_NAME, "aTexCoord");
    gPostProcessShaderInputs[cnt].location = cnt;
    gPostProcessShaderInputs[cnt].size = 2;
    ++cnt;
}

static void gfx_init_shader_bindings() {
    gShaderBindings = calloc(MAX_SHADER_BINDINGS, sizeof(struct ShaderBinding));
    if (!gShaderBindings) {
        sys_fatal("Failed to allocate shader bindings, ran out of memory!");
    }

    gPostProcessShaderBindings = calloc(MAX_SHADER_BINDINGS, sizeof(struct ShaderBinding));
    if (!gPostProcessShaderBindings) {
        free(gShaderBindings);
        gShaderBindings = NULL;
        sys_fatal("Failed to allocate post process shader bindings, ran out of memory!");
    }

    int cnt = 0;

    for (int t = 0; t < 2; t++) {
        snprintf(gShaderBindings[cnt].name, MAX_SHADER_VARIABLE_NAME, "uTex%d", t);
        gShaderBindings[cnt].binding = t;
        cnt++;
    }

    snprintf(gShaderBindings[cnt].name, MAX_SHADER_VARIABLE_NAME, "uPassTex");
    gShaderBindings[cnt].binding = 10;
    cnt++;

    cnt = 0;

    snprintf(gPostProcessShaderBindings[cnt].name, MAX_SHADER_VARIABLE_NAME, "uPassTex");
    gPostProcessShaderBindings[cnt].binding = 10;
    cnt++;
}

void gfx_init_shaders() {
    gfx_init_shader_inputs();
    gfx_init_shader_bindings();
}

static void process_shader_line(struct Shader *shader, struct ShaderInput *referenceInputs, char *output, const char *line) {
    char type[32], name[MAX_SHADER_VARIABLE_NAME];

    // parse inputs for inputs equivalent to reference inputs
    if (sscanf(line, " in %31s %31[^; \t\n]", type, name) == 2) {
        for (int i = 0; i < MAX_SHADER_INPUTS; i++) {
            if (referenceInputs[i].name[0] != '\0' && strcmp(referenceInputs[i].name, name) == 0) {
                char layoutLine[sizeof(type) + MAX_SHADER_INPUT_NAME + 64];
                snprintf(layoutLine, sizeof(layoutLine), "layout(location=%d) in %s %s", referenceInputs[i].location, type, name);
                strncat(output, layoutLine, MAX_SHADER_CODE - strlen(output) - 1);
                return;
            }
        }
    }

    // look for and parse outputs
    if (sscanf(line, " out %31s %31[^; \t\n]", type, name) == 2) {
        // add name to our shader outputs
        if (shader) {
            snprintf(shader->shaderOutputs[sShaderOutputCount].name, MAX_SHADER_OUTPUT_NAME, "%s", name);
            shader->shaderOutputs[sShaderOutputCount].location = sShaderOutputCount;
        }
        char layoutLine[sizeof(type) + MAX_SHADER_OUTPUT_NAME + 64];
        snprintf(layoutLine, sizeof(layoutLine), "layout(location=%d) out %s %s", sShaderOutputCount, type, name);
        strncat(output, layoutLine, MAX_SHADER_CODE - strlen(output) - 1);
        sShaderOutputCount++;
        return;
    }

    strncat(output, line, MAX_SHADER_CODE - strlen(output) - 1);
}

static void gfx_sanitize_shader(struct Shader *shader, struct ShaderInput *referenceInputs, struct ShaderBinding *referenceBindings, char **shaderCode) {
    if (!shaderCode || !*shaderCode) return;

    char *sanitized = (char *)calloc(1, MAX_SHADER_CODE);
    if (!sanitized) return;

    char *sourceCopy = strdup(*shaderCode);
    char *line = sourceCopy;

    sShaderOutputCount = 0;

    while (line && *line) {
        char *lineEnd = strpbrk(line, "\n;{");

        // if no delimiter was found, process the line and exit
        if (lineEnd == 0) {
            process_shader_line(shader, referenceInputs, sanitized, line);
            break;
        }

        // save delimiter
        char delimiter = *lineEnd;
        *lineEnd = '\0';

        // process line
        process_shader_line(shader, referenceInputs, sanitized, line);

        // readd delimiter
        size_t len = strlen(sanitized);
        if (len < MAX_SHADER_CODE - 2) {
            sanitized[len] = delimiter;
            sanitized[len + 1] = '\0';
        }

        line = lineEnd + 1;
    }

    free(sourceCopy);

    // copy reference inputs and reference bindings to shader input
    if (shader) {
        memcpy(&shader->shaderInputs, referenceInputs, sizeof(struct ShaderInput[MAX_SHADER_INPUTS]));
        memcpy(&shader->shaderBindings, referenceBindings, sizeof(struct ShaderBinding[MAX_SHADER_BINDINGS]));
    }

    *shaderCode = sanitized;
}

void gfx_sanitize_vertex_shader(struct Shader *shader, struct ShaderInput *referenceInputs, struct ShaderBinding *referenceBindings, char **shaderCode) {
    gfx_sanitize_shader(shader, referenceInputs, referenceBindings, shaderCode);
}

void gfx_sanitize_fragment_shader(struct Shader *shader, struct ShaderOutput *outputsFromVertexShader, struct ShaderBinding *referenceBindings, char **shaderCode) {
    // convert outputs to inputs for fragment shader
    struct ShaderInput inputs[MAX_SHADER_INPUTS] = { 0 };
    for (int i = 0; i < MAX_SHADER_INPUTS; i++) {
        strcpy(inputs[i].name, outputsFromVertexShader[i].name);
        inputs[i].location = outputsFromVertexShader[i].location;
    }
    gfx_sanitize_shader(shader, inputs, referenceBindings, shaderCode);
}

static void process_conversion_410_to_420_line(struct ShaderBinding *referenceBindings, char *output, const char *line) {
    char type[32], name[MAX_SHADER_VARIABLE_NAME];

    // convert sampler and image to use a uniform binding
    if (sscanf(line, " uniform %31s %31[^; \t\n]", type, name) == 2) {
        if (strncmp(type, "sampler", 7) == 0 || strncmp(type, "image", 5) == 0) {
            for (int i = 0; i < MAX_SHADER_BINDINGS; i++) {
                if (referenceBindings[i].name[0] != '\0' && strcmp(referenceBindings[i].name, name) == 0) {
                    char layoutLine[128];
                    snprintf(layoutLine, sizeof(layoutLine), "layout(binding=%d) uniform %s %s", referenceBindings[i].binding, type, name);
                    strncat(output, layoutLine, MAX_SHADER_CODE - strlen(output) - 1);
                    return;
                }
            }
        }
    }

    // find and change #version 410 to #version 420
    if (strncmp(line, "#version", 8) == 0) {
        int version;
        if (sscanf(line, "#version %d", &version) == 1) {
            if (version == 410) {
                strncat(output, "#version 420", MAX_SHADER_CODE - strlen(output) - 1);
                return;
            }
        }
    }

    strncat(output, line, MAX_SHADER_CODE - strlen(output) - 1);
}

// this is some wild wizardry just to keep macOS afloat lol
static void gfx_convert_410_to_420(struct ShaderBinding *referenceBindings, char **shaderCode) {
    if (!shaderCode || !*shaderCode) return;

    char *sanitized = (char *)calloc(1, MAX_SHADER_CODE);
    if (!sanitized) return;

    char *sourceCopy = strdup(*shaderCode);
    char *line = sourceCopy;

    sShaderOutputCount = 0;

    while (line && *line) {
        char *lineEnd = strpbrk(line, "\n;{");

        // if no delimiter was found, process the line and exit
        if (lineEnd == 0) {
            process_conversion_410_to_420_line(referenceBindings, sanitized, line);
            break;
        }

        // save delimiter
        char delimiter = *lineEnd;
        *lineEnd = '\0';

        // process line
        process_conversion_410_to_420_line(referenceBindings, sanitized, line);

        // readd delimiter
        size_t len = strlen(sanitized);
        if (len < MAX_SHADER_CODE - 2) {
            sanitized[len] = delimiter;
            sanitized[len + 1] = '\0';
        }

        line = lineEnd + 1;
    }

    free(sourceCopy);
    *shaderCode = sanitized;
}

bool gfx_compile_shader_to_spirv(glslang_stage_t stage, const char *shaderCode, struct Shader *shader) {
    // convert shader to version 420
    char *shaderCode420 = strdup(shaderCode);
    if (!shaderCode420) {
        LOG_ERROR("Failed to convert shader code to version 420, ran out of memory!");
        return false;
    }
    gfx_convert_410_to_420(shader->shaderBindings, &shaderCode420);

    // target vulkan as it's a bit more stingy then modern opengl
    const glslang_input_t input = {
        .language = GLSLANG_SOURCE_GLSL,
        .stage = stage,
        .client = GLSLANG_CLIENT_VULKAN,
        .client_version = GLSLANG_TARGET_VULKAN_1_3,
        .target_language = GLSLANG_TARGET_SPV,
        .target_language_version = GLSLANG_TARGET_SPV_1_5,
        .code = shaderCode420,
        // target #version 420 core
        .default_version = 420,
        .default_profile = GLSLANG_CORE_PROFILE,
        .force_default_version_and_profile = false,
        .forward_compatible = false,
        .messages = GLSLANG_MSG_DEFAULT_BIT,
        .resource = glslang_default_resource(),
    };

    glslang_shader_t *slangShader = glslang_shader_create(&input);

    SpirVBinary bin = {
        .words = NULL,
        .size = 0,
    };
    if (!glslang_shader_preprocess(slangShader, &input)) {
        LOG_ERROR("GLSL preprocessing failed!\n%s\n%s\n%s",
            glslang_shader_get_info_log(slangShader),
            glslang_shader_get_info_debug_log(slangShader),
            input.code);
        glslang_shader_delete(slangShader);
        return false;
    }

    if (!glslang_shader_parse(slangShader, &input)) {
        LOG_ERROR("GLSL parsing failed!\n%s\n%s\n%s",
            glslang_shader_get_info_log(slangShader),
            glslang_shader_get_info_debug_log(slangShader),
            glslang_shader_get_preprocessed_code(slangShader));
        glslang_shader_delete(slangShader);
        return false;
    }

    glslang_program_t *program = glslang_program_create();
    glslang_program_add_shader(program, slangShader);

    if (!glslang_program_link(program, GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT)) {
        LOG_ERROR("GLSL linking failed!\n%s\n%s",
            glslang_shader_get_info_log(slangShader),
            glslang_shader_get_info_debug_log(slangShader));
        glslang_program_delete(program);
        glslang_shader_delete(slangShader);
        return false;
    }

    glslang_program_SPIRV_generate(program, stage);

    bin.size = glslang_program_SPIRV_get_size(program);
    // must be freed later or there will be a leak
    bin.words = malloc(bin.size * sizeof(uint32_t));
    glslang_program_SPIRV_get(program, bin.words);

    const char *spirv_messages = glslang_program_SPIRV_get_messages(program);
    if (spirv_messages) {
        LOG_INFO("%s\b", spirv_messages);
    }

    glslang_program_delete(program);
    glslang_shader_delete(slangShader);

    shader->spirVBinary = bin;

    free(shaderCode420);

    return true;
}

#define SPVC_CHECK(x) \
    _macroResult = (x); \
    if (_macroResult != SPVC_SUCCESS) { \
        LOG_ERROR("SPIRV-Cross Error: %d at %s:%d", _macroResult, __FILE__, __LINE__); \
        spvc_context_destroy(context); \
        return; \
    } \

void gfx_convert_spirv_to_hlsl(struct Shader *shader) {
    spvc_context context = NULL;
    spvc_compiler compiler = NULL;
    spvc_parsed_ir ir = NULL;
    spvc_result _macroResult; // Shader TODO: There must be a better way for the SPVC_CHECK macro
    const char *hlsl_code = NULL;

    SpirVBinary *bin = &shader->spirVBinary;

    SPVC_CHECK(spvc_context_create(&context));
    SPVC_CHECK(spvc_context_parse_spirv(context, bin->words, bin->size, &ir));

    SPVC_CHECK(spvc_context_create_compiler(context, SPVC_BACKEND_HLSL, ir, SPVC_CAPTURE_MODE_TAKE_OWNERSHIP, &compiler));

    spvc_resources resources;
    spvc_compiler_create_shader_resources(compiler, &resources);

    const spvc_reflected_resource* list;
    size_t count;
    SPVC_CHECK(spvc_resources_get_resource_list_for_type(resources, SPVC_RESOURCE_TYPE_UNIFORM_BUFFER, &list, &count));

    for (size_t i = 0; i < count; i++) {
        uint32_t binding = spvc_compiler_get_decoration(compiler, list[i].id, SpvDecorationBinding);
        printf("Found UBO: %s at Binding: %u\n", list[i].name, binding);
    }

    spvc_compiler_options options;
    spvc_compiler_create_compiler_options(compiler, &options);
    spvc_compiler_options_set_uint(options, SPVC_COMPILER_OPTION_HLSL_SHADER_MODEL, 50);
    spvc_compiler_install_compiler_options(compiler, options);

    spvc_compiler_compile(compiler, &hlsl_code);
    printf("Generated %s HLSL:\n%s\n", shader->stage == GLSLANG_STAGE_VERTEX ? "Vertex" : "Fragment", hlsl_code);

    spvc_context_destroy(context);
}

#undef SPVC_CHECK
// This is a file to abstract a shader string into a standard structure. It's used to convert GLSL to
// HLSL as well as allow mods to modify uniforms and such by name

#include <stdlib.h>
#include <string.h>

#include <PR/ultratypes.h>

#include "gfx_pc.h"
#include "gfx_shader.h"

#include "pc/debuglog.h"

struct ShaderInput *gShaderInputs = NULL;
struct ShaderInput *gPostProcessShaderInputs = NULL;
struct ShaderBinding *gShaderBindings = NULL;
struct ShaderBinding *gPostProcessShaderBindings = NULL;

const char *gDefaultPostProcessVertexShader = "#version 410 core\n"
    "in vec4 aVtxPos;\n"
    "out vec4 vVtxPos;\n" // exists for convenience of mods
    "in vec2 aTexCoord;\n"
    "out vec2 vTexCoord;\n"
    "void main() {\n"
    "    vVtxPos = aVtxPos;\n"
    "    vTexCoord = aTexCoord;\n"
    "    gl_Position = aVtxPos;\n"
    "}\n";

const char *gDefaultPostProcessFragmentShader = "#version 410 core\n"
    "uniform sampler2D uPassTex;\n"
    "in vec4 vVtxPos;\n" // exists for convenience of mods
    "in vec2 vTexCoord;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    fragColor = texture(uPassTex, vTexCoord);\n"
    "}\n";

static int sShaderOutputCount = 0;
static int sShaderBindingCount = 0;
static int sShaderUniformCount = 0;

static char sShaderUniformCode[MAX_SHADER_CODE] = { 0 };

static void append_str(char *buf, size_t *len, const char *str) {
    while (*str != '\0') buf[(*len)++] = *str++;
}

static void append_line(char *buf, size_t *len, const char *str) {
    while (*str != '\0') buf[(*len)++] = *str++;
    buf[(*len)++] = '\n';
}

static const char *shader_item_to_str(uint32_t item, bool with_alpha, bool only_alpha, bool hint_single_element) {
    if (!only_alpha) {
        switch (item) {
            case SHADER_0:
                return with_alpha ? "vec4(0.0, 0.0, 0.0, 0.0)" : "vec3(0.0, 0.0, 0.0)";
            case SHADER_1:
                return with_alpha ? "vec4(1.0, 1.0, 1.0, 1.0)" : "vec3(1.0, 1.0, 1.0)";
            case SHADER_INPUT_1:
                return with_alpha ? "vInput1" : "vInput1.rgb";
            case SHADER_INPUT_2:
                return with_alpha ? "vInput2" : "vInput2.rgb";
            case SHADER_INPUT_3:
                return with_alpha ? "vInput3" : "vInput3.rgb";
            case SHADER_INPUT_4:
                return with_alpha ? "vInput4" : "vInput4.rgb";
            case SHADER_INPUT_5:
                return with_alpha ? "vInput5" : "vInput5.rgb";
            case SHADER_INPUT_6:
                return with_alpha ? "vInput6" : "vInput6.rgb";
            case SHADER_INPUT_7:
                return with_alpha ? "vInput7" : "vInput7.rgb";
            case SHADER_INPUT_8:
                return with_alpha ? "vInput8" : "vInput8.rgb";
            case SHADER_TEXEL0:
                return with_alpha ? "texVal0" : "texVal0.rgb";
            case SHADER_TEXEL0A:
                return hint_single_element ? "texVal0.a" :
                    (with_alpha ? "vec4(texVal0.a, texVal0.a, texVal0.a, texVal0.a)" : "vec3(texVal0.a, texVal0.a, texVal0.a)");
            case SHADER_TEXEL1:
                return with_alpha ? "texVal1" : "texVal1.rgb";
            case SHADER_TEXEL1A:
                return hint_single_element ? "texVal1.a" :
                    (with_alpha ? "vec4(texVal1.a, texVal1.a, texVal1.a, texVal1.a)" : "vec3(texVal1.a, texVal1.a, texVal1.a)");
            case SHADER_COMBINED:
                return with_alpha ? "texel" : "texel.rgb";
            case SHADER_COMBINEDA:
                return hint_single_element ? "texel.a" :
                    (with_alpha ? "vec4(texel.a, texel.a, texel.a, texel.a)" : "vec3(texel.a, texel.a, texel.a)");
            case SHADER_NOISE:
                return with_alpha ? "vec4(noise)" : "vec3(noise)";
        }
    } else {
        switch (item) {
            case SHADER_0:
                return "0.0";
            case SHADER_1:
                return "1.0";
            case SHADER_INPUT_1:
                return "vInput1.a";
            case SHADER_INPUT_2:
                return "vInput2.a";
            case SHADER_INPUT_3:
                return "vInput3.a";
            case SHADER_INPUT_4:
                return "vInput4.a";
            case SHADER_INPUT_5:
                return "vInput5.a";
            case SHADER_INPUT_6:
                return "vInput6.a";
            case SHADER_INPUT_7:
                return "vInput7.a";
            case SHADER_INPUT_8:
                return "vInput8.a";
            case SHADER_TEXEL0:
                return "texVal0.a";
            case SHADER_TEXEL0A:
                return "texVal0.a";
            case SHADER_TEXEL1:
                return "texVal1.a";
            case SHADER_TEXEL1A:
                return "texVal1.a";
            case SHADER_COMBINED:
                return "texel.a";
            case SHADER_COMBINEDA:
                return "texel.a";
            case SHADER_NOISE:
                return "noise.a";
        }
    }
    return "unknown";
}

static void append_formula(char *buf, size_t *len, uint8_t* cmd, bool do_single, bool do_multiply, bool do_mix, bool with_alpha, bool only_alpha) {
    if (do_single) {
        append_str(buf, len, shader_item_to_str(cmd[only_alpha * 4 + 3], with_alpha, only_alpha, false));
    } else if (do_multiply) {
        append_str(buf, len, shader_item_to_str(cmd[only_alpha * 4 + 0], with_alpha, only_alpha, false));
        append_str(buf, len, " * ");
        append_str(buf, len, shader_item_to_str(cmd[only_alpha * 4 + 2], with_alpha, only_alpha, true));
    } else if (do_mix) {
        append_str(buf, len, "mix(");
        append_str(buf, len, shader_item_to_str(cmd[only_alpha * 4 + 1], with_alpha, only_alpha, false));
        append_str(buf, len, ", ");
        append_str(buf, len, shader_item_to_str(cmd[only_alpha * 4 + 0], with_alpha, only_alpha, false));
        append_str(buf, len, ", ");
        append_str(buf, len, shader_item_to_str(cmd[only_alpha * 4 + 2], with_alpha, only_alpha, true));
        append_str(buf, len, ")");
    } else {
        append_str(buf, len, "(");
        append_str(buf, len, shader_item_to_str(cmd[only_alpha * 4 + 0], with_alpha, only_alpha, false));
        append_str(buf, len, " - ");
        append_str(buf, len, shader_item_to_str(cmd[only_alpha * 4 + 1], with_alpha, only_alpha, false));
        append_str(buf, len, ") * ");
        append_str(buf, len, shader_item_to_str(cmd[only_alpha * 4 + 2], with_alpha, only_alpha, true));
        append_str(buf, len, " + ");
        append_str(buf, len, shader_item_to_str(cmd[only_alpha * 4 + 3], with_alpha, only_alpha, false));
    }
}

char *gfx_generate_default_vertex_shader_from_cc(UNUSED struct ColorCombiner *cc) {
    static char vs_buf[8192] = { 0 };
    size_t vs_len = 0;

    append_line(vs_buf, &vs_len, "#version 410 core");
    append_line(vs_buf, &vs_len, "in vec4 aVtxPos;");
    for (int t = 0; t < 2; t++) {
        vs_len += sprintf(vs_buf + vs_len, "in vec2 aTexCoord%d;\n", t);
        vs_len += sprintf(vs_buf + vs_len, "out vec2 vTexCoord%d;\n", t);
    }
    append_line(vs_buf, &vs_len, "in vec4 aFog;");
    append_line(vs_buf, &vs_len, "out vec4 vFog;");
    append_line(vs_buf, &vs_len, "in vec2 aLightMap;");
    append_line(vs_buf, &vs_len, "out vec2 vLightMap;");
    for (int i = 0; i < CC_MAX_INPUTS; i++) {
        vs_len += sprintf(vs_buf + vs_len, "in vec4 aInput%d;\n", i + 1);
        vs_len += sprintf(vs_buf + vs_len, "out vec4 vInput%d;\n", i + 1);
    }
    append_line(vs_buf, &vs_len, "in vec3 aNormal;");
    append_line(vs_buf, &vs_len, "out vec3 vNormal;");
    append_line(vs_buf, &vs_len, "in vec3 aBarycentric;");
    append_line(vs_buf, &vs_len, "out vec3 vBarycentric;");
    append_line(vs_buf, &vs_len, "void main() {");
    for (int t = 0; t < 2; t++) {
        vs_len += sprintf(vs_buf + vs_len, "vTexCoord%d = aTexCoord%d;\n", t, t);
    }
    append_line(vs_buf, &vs_len, "vFog = aFog;");
    append_line(vs_buf, &vs_len, "vLightMap = aLightMap;");
    for (int i = 0; i < CC_MAX_INPUTS; i++) {
        vs_len += sprintf(vs_buf + vs_len, "vInput%d = aInput%d;\n", i + 1, i + 1);
    }
    append_line(vs_buf, &vs_len, "vNormal = aNormal;");
    append_line(vs_buf, &vs_len, "vBarycentric = aBarycentric;");
    append_line(vs_buf, &vs_len, "gl_Position = aVtxPos;");
    append_line(vs_buf, &vs_len, "}");

    vs_buf[vs_len] = '\0';

    return vs_buf;
}

char *gfx_generate_default_fragment_shader_from_cc(struct ColorCombiner *cc) {
    struct CCFeatures ccf = { 0 };
    gfx_cc_get_features(cc, &ccf);

    bool opt_alpha = cc->cm.use_alpha;
    bool opt_fog = cc->cm.use_fog;
    bool opt_texture_edge = cc->cm.texture_edge;
    bool opt_2cycle = cc->cm.use_2cycle;
    bool opt_light_map = cc->cm.light_map;
    bool opt_tex_persp = cc->cm.tex_persp;
    bool world_geometry = cc->cm.world_geometry;
    bool opt_dither = cc->cm.use_dither;

    static char fs_buf[8192] = { 0 };
    size_t fs_len = 0;

    append_line(fs_buf, &fs_len, "#version 410 core");
    append_line(fs_buf, &fs_len, "out vec4 fragColor;");
    for (int t = 0; t < 2; t++) {
        if (!opt_tex_persp) { append_str(fs_buf, &fs_len, "noperspective "); }
        fs_len += sprintf(fs_buf + fs_len, "in vec2 vTexCoord%d;\n", t);
    }
    append_line(fs_buf, &fs_len, "in vec4 vFog;");
    append_line(fs_buf, &fs_len, "in vec2 vLightMap;");
    for (int i = 0; i < CC_MAX_INPUTS; i++) {
        fs_len += sprintf(fs_buf + fs_len, "in vec4 vInput%d;\n", i + 1);
    }

    for (int t = 0; t < 2; t++) {
        if (ccf.used_textures[t]) {
            fs_len += sprintf(fs_buf + fs_len, "uniform sampler2D uTex%d;\n", t);
            fs_len += sprintf(fs_buf + fs_len, "uniform vec2 uTex%dSize;\n", t);
            fs_len += sprintf(fs_buf + fs_len, "uniform bool uTex%dFilter;\n", t);
        }
    }

    // 3 point texture filtering
    // Original author: ArthurCarvalho
    // Modified GLSL implementation by twinaphex, mupen64plus-libretro project.
    if (ccf.used_textures[0] || ccf.used_textures[1]) {
        append_line(fs_buf, &fs_len, "#define TEX_OFFSET(off) texture(tex, texCoord - (off)/texSize)");
        append_line(fs_buf, &fs_len, "vec4 filter3point(in sampler2D tex, in vec2 texCoord, in vec2 texSize) {");
        append_line(fs_buf, &fs_len, "    vec2 offset = fract(texCoord*texSize - vec2(0.5));");
        append_line(fs_buf, &fs_len, "    offset -= step(1.0, offset.x + offset.y);");
        append_line(fs_buf, &fs_len, "    vec4 c0 = TEX_OFFSET(offset);");
        append_line(fs_buf, &fs_len, "    vec4 c1 = TEX_OFFSET(vec2(offset.x - sign(offset.x), offset.y));");
        append_line(fs_buf, &fs_len, "    vec4 c2 = TEX_OFFSET(vec2(offset.x, offset.y - sign(offset.y)));");
        append_line(fs_buf, &fs_len, "    return c0 + abs(offset.x)*(c1-c0) + abs(offset.y)*(c2-c0);");
        append_line(fs_buf, &fs_len, "}");
        append_line(fs_buf, &fs_len, "vec4 sampleTex(in sampler2D tex, in vec2 uv, in vec2 texSize, in bool dofilter, in int filterType) {");
        append_line(fs_buf, &fs_len, "    if (dofilter && filterType == 2)");
        append_line(fs_buf, &fs_len, "        return filter3point(tex, uv, texSize);");
        append_line(fs_buf, &fs_len, "    else");
        append_line(fs_buf, &fs_len, "        return texture(tex, uv);");
        append_line(fs_buf, &fs_len, "}");
    }

    if (world_geometry) {
        append_line(fs_buf, &fs_len, "float dither4x4(vec2 position, float brightness) {");
        append_line(fs_buf, &fs_len, "    int x = int(mod(position.x, 4.0));");
        append_line(fs_buf, &fs_len, "    int y = int(mod(position.y, 4.0));");
        append_line(fs_buf, &fs_len, "    int index = x + y * 4;");
        append_line(fs_buf, &fs_len, "    float limit = 0.0;");
        append_line(fs_buf, &fs_len, "    if (x < 8) {");
        append_line(fs_buf, &fs_len, "        if (index == 0) limit = 0.0625;");
        append_line(fs_buf, &fs_len, "        if (index == 1) limit = 0.5625;");
        append_line(fs_buf, &fs_len, "        if (index == 2) limit = 0.1875;");
        append_line(fs_buf, &fs_len, "        if (index == 3) limit = 0.6875;");
        append_line(fs_buf, &fs_len, "        if (index == 4) limit = 0.8125;");
        append_line(fs_buf, &fs_len, "        if (index == 5) limit = 0.3125;");
        append_line(fs_buf, &fs_len, "        if (index == 6) limit = 0.9375;");
        append_line(fs_buf, &fs_len, "        if (index == 7) limit = 0.4375;");
        append_line(fs_buf, &fs_len, "        if (index == 8) limit = 0.25;");
        append_line(fs_buf, &fs_len, "        if (index == 9) limit = 0.75;");
        append_line(fs_buf, &fs_len, "        if (index == 10) limit = 0.125;");
        append_line(fs_buf, &fs_len, "        if (index == 11) limit = 0.625;");
        append_line(fs_buf, &fs_len, "        if (index == 12) limit = 1.0;");
        append_line(fs_buf, &fs_len, "        if (index == 13) limit = 0.5;");
        append_line(fs_buf, &fs_len, "        if (index == 14) limit = 0.875;");
        append_line(fs_buf, &fs_len, "        if (index == 15) limit = 0.375;");
        append_line(fs_buf, &fs_len, "    }");
        append_line(fs_buf, &fs_len, "    return brightness < limit ? 0.0 : 1.0;");
        append_line(fs_buf, &fs_len, "}");

        append_line(fs_buf, &fs_len, "vec3 rgb2hsv(vec3 c) {");
        append_line(fs_buf, &fs_len, "    vec4 K = vec4(0.0, -1.0/3.0, 2.0/3.0, -1.0);");
        append_line(fs_buf, &fs_len, "    vec4 p = mix(vec4(c.bg, K.wz),");
        append_line(fs_buf, &fs_len, "                 vec4(c.gb, K.xy),");
        append_line(fs_buf, &fs_len, "                 step(c.b, c.g));");
        append_line(fs_buf, &fs_len, "    vec4 q = mix(vec4(p.xyw, c.r),");
        append_line(fs_buf, &fs_len, "                 vec4(c.r, p.yzx),");
        append_line(fs_buf, &fs_len, "                 step(p.x, c.r));");
        append_line(fs_buf, &fs_len, "    float d = q.x - min(q.w, q.y);");
        append_line(fs_buf, &fs_len, "    float e = 1.0e-10;");
        append_line(fs_buf, &fs_len, "    return vec3(");
        append_line(fs_buf, &fs_len, "        abs(q.z + (q.w - q.y) / (6.0 * d + e)), // hue");
        append_line(fs_buf, &fs_len, "        d / (q.x + e),                          // saturation");
        append_line(fs_buf, &fs_len, "        q.x                                     // value");
        append_line(fs_buf, &fs_len, "    );");
        append_line(fs_buf, &fs_len, "}");
        append_line(fs_buf, &fs_len, "");
        append_line(fs_buf, &fs_len, "vec3 hsv2rgb(vec3 c) {");
        append_line(fs_buf, &fs_len, "    vec3 p = abs(fract(c.xxx + vec3(0.0, 2.0/3.0, 1.0/3.0)) * 6.0 - 3.0);");
        append_line(fs_buf, &fs_len, "    return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y);");
        append_line(fs_buf, &fs_len, "}");
    }

    if ((opt_alpha && opt_dither) || ccf.do_noise) {
        append_line(fs_buf, &fs_len, "uniform float uFrameCount;");

        append_line(fs_buf, &fs_len, "float random(in vec3 value) {");
        append_line(fs_buf, &fs_len, "    float random = dot(sin(value), vec3(12.9898, 78.233, 37.719));");
        append_line(fs_buf, &fs_len, "    return fract(sin(random) * 143758.5453);");
        append_line(fs_buf, &fs_len, "}");
    }

    if (opt_light_map) {
        append_line(fs_buf, &fs_len, "uniform vec3 uLightmapColor;");
    }

    if (world_geometry) {
        fs_len += sprintf(fs_buf + fs_len, "uniform int uShaderFlags[%d];\n", SHADER_FLAG_MAX);
        fs_len += sprintf(fs_buf + fs_len, "uniform float uShaderFlagValues[%d];\n", SHADER_FLAG_MAX);
    }

    append_line(fs_buf, &fs_len, "uniform int uFilter;");

    append_line(fs_buf, &fs_len, "void main() {");

    if ((opt_alpha && opt_dither) || ccf.do_noise) {
        append_line(fs_buf, &fs_len, "float noise = floor(random(floor(vec3(gl_FragCoord.xy, uFrameCount))) + 0.5);");
    }

    if (ccf.used_textures[0]) {
        append_line(fs_buf, &fs_len, "vec4 texVal0 = sampleTex(uTex0, vTexCoord0, uTex0Size, uTex0Filter, uFilter);");
    }
    if (ccf.used_textures[1]) {
        if (opt_light_map) {
            append_line(fs_buf, &fs_len, "vec4 texVal1 = sampleTex(uTex1, vLightMap, uTex1Size, uTex1Filter, uFilter);");
            append_line(fs_buf, &fs_len, "texVal0.rgb *= uLightmapColor.rgb;");
            append_line(fs_buf, &fs_len, "texVal1.rgb = texVal1.rgb * texVal1.rgb + texVal1.rgb;");
        } else {
            append_line(fs_buf, &fs_len, "vec4 texVal1 = sampleTex(uTex1, vTexCoord1, uTex1Size, uTex1Filter, uFilter);");
        }
    }

    append_str(fs_buf, &fs_len, (opt_alpha) ? "vec4 texel = " : "vec3 texel = ");
    for (int i = 0; i < (opt_2cycle + 1); i++) {
        u8* cmd = &cc->shader_commands[i * 8];
        if (!ccf.color_alpha_same[i] && opt_alpha) {
            append_str(fs_buf, &fs_len, "vec4(");
            append_formula(fs_buf, &fs_len, cmd, ccf.do_single[i*2+0], ccf.do_multiply[i*2+0], ccf.do_mix[i*2+0], false, false);
            append_str(fs_buf, &fs_len, ", ");
            append_formula(fs_buf, &fs_len, cmd, ccf.do_single[i*2+1], ccf.do_multiply[i*2+1], ccf.do_mix[i*2+1], true, true);
            append_str(fs_buf, &fs_len, ")");
        } else {
            append_formula(fs_buf, &fs_len, cmd, ccf.do_single[i*2+0], ccf.do_multiply[i*2+0], ccf.do_mix[i*2+0], opt_alpha, false);
        }
        append_line(fs_buf, &fs_len, ";");

        if (i == 0 && opt_2cycle) {
            append_str(fs_buf, &fs_len, "texel = ");
        }
    }

    if (opt_texture_edge && opt_alpha) {
        append_line(fs_buf, &fs_len, "if (texel.a > 0.3) texel.a = 1.0; else discard;");
    }

    // TODO discard if alpha is 0?

    if (world_geometry) {
        // hue
        append_line(fs_buf, &fs_len, "if (uShaderFlags[0] == 1) {");
        append_line(fs_buf, &fs_len, "vec3 hsv = rgb2hsv(texel.rgb);");
        append_line(fs_buf, &fs_len, "hsv.x = fract(hsv.x + uShaderFlagValues[0]);");
        append_line(fs_buf, &fs_len, "vec3 finalColor = hsv2rgb(hsv);");
        append_line(fs_buf, &fs_len, "texel.rgb = finalColor;");
        append_line(fs_buf, &fs_len, "}");

        // saturation
        append_line(fs_buf, &fs_len, "if (uShaderFlags[1] == 1) {");
        append_line(fs_buf, &fs_len, "const vec3 w = vec3(0.2125, 0.7154, 0.0721);");
        append_line(fs_buf, &fs_len, "vec3 intensity = vec3(dot(texel.rgb, w));");
        append_line(fs_buf, &fs_len, "texel.rgb = mix(intensity, texel.rgb, uShaderFlagValues[1]);");
        append_line(fs_buf, &fs_len, "}");

        // brightness
        append_line(fs_buf, &fs_len, "if (uShaderFlags[2] == 1) {");
        append_line(fs_buf, &fs_len, "texel.rgb *= uShaderFlagValues[2];");
        append_line(fs_buf, &fs_len, "}");

        // contrast
        append_line(fs_buf, &fs_len, "if (uShaderFlags[3] == 1) {");
        append_line(fs_buf, &fs_len, "texel.rgb = 0.5 + uShaderFlagValues[3] * (texel.rgb - 0.5);");
        append_line(fs_buf, &fs_len, "}");

        // exposure
        append_line(fs_buf, &fs_len, "if (uShaderFlags[4] == 1) {");
        append_line(fs_buf, &fs_len, "texel.rgb = texel.rgb + (uShaderFlagValues[4] - 2) * texel.rgb + texel.rgb;");
        append_line(fs_buf, &fs_len, "}");

        // dithering
        append_line(fs_buf, &fs_len, "if (uShaderFlags[5] == 1) {");
        append_line(fs_buf, &fs_len, "texel.rgb *= dither4x4(gl_FragCoord.xy, dot(texel.rgb, vec3(0.299, 0.587, 0.114)));");
        append_line(fs_buf, &fs_len, "}");

        // posterization
        append_line(fs_buf, &fs_len, "if (uShaderFlags[6] == 1) {");
        append_line(fs_buf, &fs_len, "int levels = int(max(1.0, uShaderFlagValues[6]));");
        append_line(fs_buf, &fs_len, "texel.rgb = floor(texel.rgb * levels) / levels;");
        append_line(fs_buf, &fs_len, "}");

        // scan lines
        append_line(fs_buf, &fs_len, "if (uShaderFlags[7] == 1) {");
        append_line(fs_buf, &fs_len, "float scan = sin(gl_FragCoord.y * 1.5) * 0.04;");
        append_line(fs_buf, &fs_len, "texel.rgb -= scan * uShaderFlagValues[7];");
        append_line(fs_buf, &fs_len, "}");
    }

    if (opt_fog) {
        if (opt_alpha) {
            append_line(fs_buf, &fs_len, "texel = vec4(mix(texel.rgb, vFog.rgb, vFog.a), texel.a);");
        } else {
            append_line(fs_buf, &fs_len, "texel = mix(texel, vFog.rgb, vFog.a);");
        }
    }

    if (opt_alpha && opt_dither) {
        append_line(fs_buf, &fs_len, "texel.a *= noise;");
    }

    if (opt_alpha) {
        append_line(fs_buf, &fs_len, "fragColor = texel;");
    } else {
        append_line(fs_buf, &fs_len, "fragColor = vec4(texel, 1.0);");
    }
    append_line(fs_buf, &fs_len, "}");

    fs_buf[fs_len] = '\0';

    return fs_buf;
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

    snprintf(gShaderInputs[cnt].name, MAX_SHADER_VARIABLE_NAME, "aVtxPos");
    gShaderInputs[cnt].location = cnt;
    gShaderInputs[cnt].size = 4;
    cnt++;

    for (int t = 0; t < 2; t++) {
        snprintf(gShaderInputs[cnt].name, MAX_SHADER_VARIABLE_NAME, "aTexCoord%d", t);
        gShaderInputs[cnt].location = cnt;
        gShaderInputs[cnt].size = 2;
        ++cnt;
    }

    snprintf(gShaderInputs[cnt].name, MAX_SHADER_VARIABLE_NAME, "aFog");
    gShaderInputs[cnt].location = cnt;
    gShaderInputs[cnt].size = 4;
    ++cnt;

    snprintf(gShaderInputs[cnt].name, MAX_SHADER_VARIABLE_NAME, "aLightMap");
    gShaderInputs[cnt].location = cnt;
    gShaderInputs[cnt].size = 2;
    ++cnt;

    for (int i = 0; i < CC_MAX_INPUTS; i++) {
        snprintf(gShaderInputs[cnt].name, MAX_SHADER_VARIABLE_NAME, "aInput%d", i + 1);
        gShaderInputs[cnt].location = cnt;
        gShaderInputs[cnt].size = 4;
        ++cnt;
    }

    snprintf(gShaderInputs[cnt].name, MAX_SHADER_VARIABLE_NAME, "aNormal");
    gShaderInputs[cnt].location = cnt;
    gShaderInputs[cnt].size = 3;
    ++cnt;

    snprintf(gShaderInputs[cnt].name, MAX_SHADER_VARIABLE_NAME, "aBarycentric");
    gShaderInputs[cnt].location = cnt;
    gShaderInputs[cnt].size = 3;
    ++cnt;

    // post process shader inputs
    cnt = 0;

    snprintf(gPostProcessShaderInputs[cnt].name, MAX_SHADER_VARIABLE_NAME, "aVtxPos");
    gPostProcessShaderInputs[cnt].location = cnt;
    gPostProcessShaderInputs[cnt].size = 4;
    ++cnt;

    snprintf(gPostProcessShaderInputs[cnt].name, MAX_SHADER_VARIABLE_NAME, "aTexCoord");
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
    char qualifier[32] = { 0 }, type[32] = { 0 }, name[MAX_SHADER_VARIABLE_NAME] = { 0 };

    // parse inputs for inputs equivalent to reference inputs
    if (sscanf(line, "%31s in %31s %31[^; \t\n]", qualifier, type, name) == 3) {
        for (int i = 0; i < MAX_SHADER_INPUTS; i++) {
            if (referenceInputs[i].name[0] != '\0' && strcmp(referenceInputs[i].name, name) == 0) {
                char layoutLine[sizeof(type) + MAX_SHADER_VARIABLE_NAME + 64];
                snprintf(layoutLine, sizeof(layoutLine), "%s layout(location=%d) in %s %s", qualifier, referenceInputs[i].location, type, name);
                strncat(output, layoutLine, MAX_SHADER_CODE - strlen(output) - 1);
                return;
            }
        }
    }

    if (sscanf(line, " in %31s %31[^; \t\n]", type, name) == 2) {
        for (int i = 0; i < MAX_SHADER_INPUTS; i++) {
            if (referenceInputs[i].name[0] != '\0' && strcmp(referenceInputs[i].name, name) == 0) {
                char layoutLine[sizeof(type) + MAX_SHADER_VARIABLE_NAME + 64];
                snprintf(layoutLine, sizeof(layoutLine), "layout(location=%d) in %s %s", referenceInputs[i].location, type, name);
                strncat(output, layoutLine, MAX_SHADER_CODE - strlen(output) - 1);
                return;
            }
        }
    }

    // look for and parse outputs
    if (sscanf(line, "%31s out %31s %31[^; \t\n]", qualifier, type, name) == 3) {
        // add name to our shader outputs
        if (shader) {
            snprintf(shader->shaderOutputs[sShaderOutputCount].name, MAX_SHADER_VARIABLE_NAME, "%s", name);
            shader->shaderOutputs[sShaderOutputCount].location = sShaderOutputCount;
        }
        char layoutLine[sizeof(type) + MAX_SHADER_VARIABLE_NAME + 64];
        snprintf(layoutLine, sizeof(layoutLine), "%s layout(location=%d) out %s %s", qualifier, sShaderOutputCount, type, name);
        strncat(output, layoutLine, MAX_SHADER_CODE - strlen(output) - 1);
        if (sShaderOutputCount < MAX_SHADER_OUTPUTS - 1) { sShaderOutputCount++; }
        return;
    }

    if (sscanf(line, " out %31s %31[^; \t\n]", type, name) == 2) {
        // add name to our shader outputs
        if (shader) {
            snprintf(shader->shaderOutputs[sShaderOutputCount].name, MAX_SHADER_VARIABLE_NAME, "%s", name);
            shader->shaderOutputs[sShaderOutputCount].location = sShaderOutputCount;
        }
        char layoutLine[sizeof(type) + MAX_SHADER_VARIABLE_NAME + 64];
        snprintf(layoutLine, sizeof(layoutLine), "layout(location=%d) out %s %s", sShaderOutputCount, type, name);
        strncat(output, layoutLine, MAX_SHADER_CODE - strlen(output) - 1);
        if (sShaderOutputCount < MAX_SHADER_OUTPUTS - 1) { sShaderOutputCount++; }
        return;
    }

    // look for and parse shader uniforms
    if (sscanf(line, " uniform %31s %31[^; \t\n]", type, name) == 2) {
        if (strncmp(type, "sampler", 7) == 0 || strncmp(type, "image", 5) == 0) {
            if (shader) {
                // add to shader bindings
                snprintf(shader->shaderBindings[sShaderBindingCount].name, MAX_SHADER_VARIABLE_NAME, "%s", name);
                shader->shaderBindings[sShaderBindingCount].binding = sShaderBindingCount;
            }
            if (sShaderBindingCount < MAX_SHADER_BINDINGS - 1) { sShaderBindingCount++; }
        } else if (shader) {
            // add to shader uniforms
            snprintf(shader->shaderUniforms[sShaderUniformCount].name, MAX_SHADER_VARIABLE_NAME, "%s", name);
            shader->shaderUniforms[sShaderUniformCount].location = sShaderUniformCount;
            if (sShaderUniformCount < MAX_SHADER_UNIFORMS - 1) { sShaderUniformCount++; }
        }
    }

    // look for and override version number
    if (sscanf(line, "#version %31s %31s", type, name) == 2
    ||  sscanf(line, " #version %31s %31s", type, name) == 2
    ||  sscanf(line, "#version %31s", type) == 1
    ||  sscanf(line, " #version %31s", type) == 1) {
        char layoutLine[32] = { 0 };
        snprintf(layoutLine, sizeof(layoutLine), "#version 410 core");
        strncat(output, layoutLine, MAX_SHADER_CODE - strlen(output) - 1);
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
    sShaderBindingCount = 0;
    sShaderUniformCount = 0;

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
        strncpy(inputs[i].name, outputsFromVertexShader[i].name, MAX_SHADER_VARIABLE_NAME - 1);
        inputs[i].location = outputsFromVertexShader[i].location;
    }
    gfx_sanitize_shader(shader, inputs, referenceBindings, shaderCode);
}

static bool process_conversion_410_to_450_line(struct ShaderBinding *referenceBindings, char *output, const char *line) {
    char type[32], name[MAX_SHADER_VARIABLE_NAME];

    // convert sampler and image to use a uniform binding
    if (sscanf(line, " uniform %31s %31[^; \t\n]", type, name) == 2) {
        if (strncmp(type, "sampler", 7) == 0 || strncmp(type, "image", 5) == 0) {
            for (int i = 0; i < MAX_SHADER_BINDINGS; i++) {
                if (referenceBindings[i].name[0] != '\0' && strcmp(referenceBindings[i].name, name) == 0) {
                    char layoutLine[sizeof(type) + MAX_SHADER_VARIABLE_NAME + 64];
                    snprintf(layoutLine, sizeof(layoutLine), "layout(binding=%d) uniform %s %s", referenceBindings[i].binding, type, name);
                    strncat(output, layoutLine, MAX_SHADER_CODE - strlen(output) - 1);
                    return true;
                }
            }
        } else {
            char layoutLine[sizeof(type) + MAX_SHADER_VARIABLE_NAME + 64];
            snprintf(layoutLine, sizeof(layoutLine), "uniform %s %s;\n", type, name);
            strncat(sShaderUniformCode, layoutLine, MAX_SHADER_CODE - strlen(sShaderUniformCode) - 1);
            if (sShaderUniformCount < MAX_SHADER_UNIFORMS - 1) { sShaderUniformCount++; }
            return false;
        }
    }

    int version = 0;

    // look for and override version number
    if (sscanf(line, "#version %d %s", &version, name) == 2) {
        char layoutLine[32] = { 0 };
        snprintf(layoutLine, sizeof(layoutLine), "#version 450 core");
        strncat(output, layoutLine, MAX_SHADER_CODE - strlen(output) - 1);
        LOG_CONSOLE("converted version to 450 core")
        return true;
    }

    strncat(output, line, MAX_SHADER_CODE - strlen(output) - 1);
    return true;
}

// this is some wild wizardry just to keep macOS afloat lol
static void gfx_convert_410_to_450(struct ShaderBinding *referenceBindings, char **shaderCode) {
    if (!shaderCode || !*shaderCode) { return; }

    char *sanitized = (char *)calloc(1, MAX_SHADER_CODE);
    if (!sanitized) { return; }

    char *sourceCopy = strdup(*shaderCode);
    char *line = sourceCopy;

    sShaderOutputCount = 0;
    sShaderBindingCount = 0;
    sShaderUniformCount = 0;

    memset(sShaderUniformCode, 0, sizeof(char) * MAX_SHADER_CODE);

    while (line && *line) {
        char *lineEnd = strpbrk(line, "\n;{");

        // if no delimiter was found, process the line and exit
        if (lineEnd == 0) {
            process_conversion_410_to_450_line(referenceBindings, sanitized, line);
            break;
        }

        // save delimiter
        char delimiter = *lineEnd;
        *lineEnd = '\0';

        // process line and readd delimiter
        if (process_conversion_410_to_450_line(referenceBindings, sanitized, line)) {
            size_t len = strlen(sanitized);
            if (len < MAX_SHADER_CODE - 2) {
                sanitized[len] = delimiter;
                sanitized[len + 1] = '\0';
            }
        }

        line = lineEnd + 1;
    }

    char *versionText = "#version 450 core";

    // readd the uniforms into a block
    if (sShaderUniformCode[0] != '\0' && strncmp(sanitized, versionText, 17) == 0) {
        // Shader TODO: is this a good method?
        char *sanitizedSource = strdup(sanitized);
        if (!sanitizedSource) {
            free(sourceCopy);
            free(sanitized);
            return;
        }

        // zero out sanitized string
        memset(sanitized, 0, sizeof(char) * MAX_SHADER_CODE);

        // append version string
        strncat(sanitized, versionText, MAX_SHADER_CODE - 1);
        strncat(sanitized, "\n", MAX_SHADER_CODE - 1);

        // append block
        strncat(sanitized, "layout(std140, set = 0, binding = 0) uniform ShaderUniforms {\n", MAX_SHADER_CODE - 1);

        // append uniform code
        strncat(sanitized, sShaderUniformCode, MAX_SHADER_CODE - 1);

        // cleanup block
        strncat(sanitized, "};\n", MAX_SHADER_CODE - 1);

        // append rest of code
        strncat(sanitized, sanitizedSource + 17, MAX_SHADER_CODE - 1);

        // cleanup
        free(sanitizedSource);
    }

    free(sourceCopy);
    *shaderCode = sanitized;
}

bool gfx_compile_shader_to_spirv(glslang_stage_t stage, const char *shaderCode, struct Shader *shader) {
    // convert shader to version 450
    char *shaderCode450 = strdup(shaderCode);
    if (!shaderCode450) {
        LOG_ERROR("Failed to convert shader code to version 450, ran out of memory!");
        return false;
    }
    gfx_convert_410_to_450(shader->shaderBindings, &shaderCode450);

    // target vulkan as it's a bit more stingy then modern opengl
    const glslang_input_t input = {
        .language = GLSLANG_SOURCE_GLSL,
        .stage = stage,
        .client = GLSLANG_CLIENT_VULKAN,
        .client_version = GLSLANG_TARGET_VULKAN_1_3,
        .target_language = GLSLANG_TARGET_SPV,
        .target_language_version = GLSLANG_TARGET_SPV_1_5,
        .code = shaderCode450,
        // target #version 450 core
        .default_version = 450,
        .default_profile = GLSLANG_CORE_PROFILE,
        .force_default_version_and_profile = false,
        .forward_compatible = false,
        .messages = GLSLANG_MSG_DEFAULT_BIT,
        .resource = glslang_default_resource(),
    };

    glslang_shader_t *slangShader = glslang_shader_create(&input);

    SpirVShader spirvShader = {
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

    spirvShader.size = glslang_program_SPIRV_get_size(program);
    // must be freed later or there will be a leak
    spirvShader.words = malloc(spirvShader.size * sizeof(uint32_t));
    glslang_program_SPIRV_get(program, spirvShader.words);

    const char *spirv_messages = glslang_program_SPIRV_get_messages(program);
    if (spirv_messages) {
        LOG_INFO("%s\b", spirv_messages);
    }

    glslang_program_delete(program);
    glslang_shader_delete(slangShader);

    shader->spirVShader = spirvShader;

    free(shaderCode450);

    return true;
}

#define SPVC_CHECK(x) \
    _macroResult = (x); \
    if (_macroResult != SPVC_SUCCESS) { \
        LOG_ERROR("SPIRV-Cross Error: %d at %s:%d", _macroResult, __FILE__, __LINE__); \
        spvc_context_destroy(context); \
        return; \
    } \

void gfx_convert_spirv_to_hlsl(char **shaderCode, struct Shader *shader) {
    spvc_context context = NULL;
    spvc_compiler compiler = NULL;
    spvc_parsed_ir ir = NULL;
    spvc_result _macroResult; // Shader TODO: There must be a better way for the SPVC_CHECK macro
    const char *hlsl_code = NULL;

    SpirVShader *spirvShader = &shader->spirVShader;

    SPVC_CHECK(spvc_context_create(&context));
    SPVC_CHECK(spvc_context_parse_spirv(context, spirvShader->words, spirvShader->size, &ir));
    SPVC_CHECK(spvc_context_create_compiler(context, SPVC_BACKEND_HLSL, ir, SPVC_CAPTURE_MODE_TAKE_OWNERSHIP, &compiler));

    spvc_resources resources;
    spvc_compiler_create_shader_resources(compiler, &resources);

    // get list of uniforms
    const spvc_reflected_resource *list;
    size_t count;
    SPVC_CHECK(spvc_resources_get_resource_list_for_type(resources, SPVC_RESOURCE_TYPE_UNIFORM_BUFFER, &list, &count));

    int uniformIndex = 0;

    for (size_t i = 0; i < count; i++) {
        // get the type and block size for the entry in the list
        spvc_type type = spvc_compiler_get_type_handle(compiler, list[i].type_id);

        size_t block_size = 0;
        spvc_compiler_get_declared_struct_size(compiler, type, &block_size);

        shader->uboTotalSize = (block_size + 15) & ~15;

        // iterate through all members, or individual uniforms
        u32 memberCount = spvc_type_get_num_member_types(type);
        for (u32 m = 0; m < memberCount; m++) {
            if (uniformIndex >= MAX_SHADER_UNIFORMS) break;

            const char *memberName = spvc_compiler_get_member_name(compiler, list[i].base_type_id, m);

            size_t memberSize = 0;
            spvc_compiler_get_declared_struct_member_size(compiler, type, m, &memberSize);

            u32 memberOffset = 0;
            spvc_compiler_type_struct_member_offset(compiler, type, m, &memberOffset);

            strncpy(shader->shaderUniforms[uniformIndex].name, memberName, MAX_SHADER_VARIABLE_NAME - 1);
            shader->shaderUniforms[uniformIndex].location = memberOffset;
            shader->shaderUniforms[uniformIndex].size = memberSize;

            uniformIndex++;
        }
    }

    spvc_compiler_options options;
    spvc_compiler_create_compiler_options(compiler, &options);
    spvc_compiler_options_set_uint(options, SPVC_COMPILER_OPTION_HLSL_SHADER_MODEL, 50);
    spvc_compiler_install_compiler_options(compiler, options);

    spvc_compiler_compile(compiler, &hlsl_code);

    *shaderCode = strdup(hlsl_code);

    spvc_context_destroy(context);
}

#undef SPVC_CHECK

void gfx_destroy_shader_contents(struct Shader *shader) {
    if (!shader) { return; }

    free(shader->spirVShader.words);
    memset(shader, 0, sizeof(struct Shader));
}

void gfx_destroy_shader(struct Shader *shader) {
    if (!shader) { return; }
    gfx_destroy_shader_contents(shader);
    free(shader);
    shader = NULL;
}
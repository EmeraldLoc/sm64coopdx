#if defined(__APPLE__)

#define NS_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

#include <SDL2/SDL.h>

#include "types.h"

#include "gfx_metal.h"

#include "pc/pc_main.h"

#include "gfx_window_manager_api.h"
#include "gfx_rendering_api.h"
#include "gfx_pc.h"
#include "gfx_shader.h"

extern "C" {
    #include "pc/debuglog.h"
    #include "pc/lua/smlua.h"
    #include "pc/mods/mods_utils.h"
    #include "gfx_sdl.h"
}

#define MAX_RING_BUFFER_SIZE 4 * 1024 * 1024

struct TextureData {
    MTL::Texture *texture = NULL;
    MTL::SamplerState *sampler = NULL;

    uint32_t width;
    uint32_t height;
    bool linearFiltering;
};

struct ShaderProgramMetal {
    MTL::RenderPipelineState *pipeline;

    MTL::Buffer *vertexConstantBuffer;
    MTL::Buffer *fragmentConstantBuffer;

    struct Shader *vertexShader;
    struct Shader *fragmentShader;

    uint8_t *vertexUniformBuffer;
    uint8_t *fragmentUniformBuffer;

    int vertexUboSize;
    int fragmentUboSize;

    uint64_t hash;

    uint8_t numInputs;
    uint8_t numFloats;

    bool usedTextures[2];
    bool usedLightmap;
    bool usedFog;
    bool worldGeometry;
};

static struct {
    SDL_MetalView metalView;

    MTL::Device* device;
    MTL::CommandQueue *commandQueue;
    MTL::CommandBuffer *commandBuffer;
    MTL::RenderCommandEncoder *encoder;
    CA::MetalLayer *layer;
    CA::MetalDrawable *drawable;
    MTL::Texture *depthTexture;
    MTL::DepthStencilState *depthStencilState;
    MTL::Buffer *vertexBuffer;
    MTL::Library *library;

    MTL::Buffer *dynamicRingBuffer[MAX_FRAME_PASSES];
    size_t ringBufferOffset[MAX_FRAME_PASSES];

    struct ShaderProgramMetal shaderProgramPool[MAX_FRAME_PASSES][CC_MAX_SHADERS];
    u8 shaderProgramPoolSize[MAX_FRAME_PASSES] = { 0 };
    u8 shaderProgramPoolIndex[MAX_FRAME_PASSES] = { 0 };

    struct ShaderProgramMetal postProcessShaderProgramPool[MAX_FRAME_PASSES];

    std::vector<struct TextureData> textures;
    int currentTile;
    u32 currentTextureIds[2];

    // Current state

    struct ShaderProgramMetal *shaderProgram;

    u32 currentWidth, currentHeight;

    s8 depthTest;
    s8 depthMask;
    s8 zModeDecal;
    bool useAlpha;

    // Previous states (to prevent setting states needlessly)

    struct ShaderProgramMetal *lastShaderProgram = NULL;
    u32 lastVertexBufferStride = 0;
    MTL::RenderPipelineState *lastPipelineState;
    MTL::Texture *lastTextures[2];
    MTL::SamplerState *lastSamplers[2];
    MTL::SamplerState *defaultSampler;
    s8 lastDepthTest = -1;
    s8 lastDepthMask = -1;
    s8 lastZModeDecal = -1;
    MTL::PrimitiveType lastPrimitiveType = MTL::PrimitiveTypeTriangle;
} metal;

static MTL::Library *metal_compile_source(const char *sourceCode, const char *stageName) {
    auto nsSource = NS::String::string(sourceCode, NS::UTF8StringEncoding);
    auto compileOptions = MTL::CompileOptions::alloc()->init();
    NS::Error *error = NULL;

    MTL::Library *library = metal.device->newLibrary(nsSource, compileOptions, &error);
    compileOptions->release();

    if (error) {
        // TODO: When possible, make printf LOG_ERROR
        const char *errorString = error->localizedDescription()->utf8String();
        printf("Metal %s compilation failed: %s\n", stageName, errorString);
        sys_fatal("Shader failed to compile!");
    }
    return library;
}

static MTL::RenderPipelineState *metal_create_pipeline(MTL::RenderPipelineDescriptor *desc) {
    NS::Error *error = NULL;
    MTL::RenderPipelineState *pipeline = metal.device->newRenderPipelineState(desc, &error);

    if (error) {
        // TODO: When possible, make printf LOG_ERROR
        const char *errorString = error->localizedDescription()->utf8String();
        printf("Failed to create render pipeline state: %s\n", errorString);
        sys_fatal("Pipeline failed to be created!");
    }
    return pipeline;
}

static MTL::DepthStencilState *get_or_create_depth_stencil_state(bool depthTest, bool depthMask) {
    auto desc = MTL::DepthStencilDescriptor::alloc()->init();
    desc->setDepthCompareFunction(depthTest ? MTL::CompareFunctionLessEqual : MTL::CompareFunctionAlways);
    desc->setDepthWriteEnabled(depthMask);

    MTL::DepthStencilState *state = metal.device->newDepthStencilState(desc);
    desc->release();
    return state;
}

static void create_depth_texture() {
    if (metal.depthTexture) {
        metal.depthTexture->release();
        metal.depthTexture = NULL;
    }

    MTL::TextureDescriptor *desc = MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatDepth32Float, metal.currentWidth, metal.currentHeight, false);

    desc->setStorageMode(MTL::StorageModePrivate);
    desc->setUsage(MTL::TextureUsageRenderTarget);

    metal.depthTexture = metal.device->newTexture(desc);

    desc->release();

    if (!metal.depthTexture) {
        sys_fatal("Failed to create Metal depth texture.");
    }
}

static void generate_uniform_buffer_metal(struct ShaderProgramMetal *prg, struct Shader *vertexShader, struct Shader *fragmentShader) {
    prg->vertexUboSize = vertexShader->uboTotalSize;
    prg->vertexUboSize = (prg->vertexUboSize + 15) & ~15;

    prg->fragmentUboSize = fragmentShader->uboTotalSize;
    prg->fragmentUboSize = (prg->fragmentUboSize + 15) & ~15;

    if (prg->vertexUboSize > 0) {
        prg->vertexUniformBuffer = (uint8_t *)malloc(prg->vertexUboSize);
        if (prg->vertexUniformBuffer == NULL) {
            sys_fatal("Failed to allocate vertex uniform buffer memory!");
        }
        memset(prg->vertexUniformBuffer, 0, prg->vertexUboSize);

        prg->vertexConstantBuffer = metal.device->newBuffer(prg->vertexUboSize, MTL::ResourceStorageModeShared);
        if (prg->vertexConstantBuffer == NULL) {
            sys_fatal("Failed to create Metal vertex constant buffer.");
        }
    } else {
        prg->vertexUniformBuffer = NULL;
        prg->vertexConstantBuffer = NULL;
    }

    if (prg->fragmentUboSize > 0) {
        prg->fragmentUniformBuffer = (uint8_t *)malloc(prg->fragmentUboSize);
        if (prg->fragmentUniformBuffer == NULL) {
            sys_fatal("Failed to allocate fragment uniform buffer memory!");
        }
        memset(prg->fragmentUniformBuffer, 0, prg->fragmentUboSize);

        prg->fragmentConstantBuffer = metal.device->newBuffer(prg->fragmentUboSize, MTL::ResourceStorageModeShared);
        if (prg->fragmentConstantBuffer == NULL) {
            sys_fatal("Failed to create Metal fragment constant buffer.");
        }
    } else {
        prg->fragmentUniformBuffer = NULL;
        prg->fragmentConstantBuffer = NULL;
    }
}

bool gfx_metal_z_is_from_0_to_1(void) {
    return true;
}

void gfx_metal_unload_shader(struct ShaderProgram *old_prg) {
}

void gfx_metal_load_shader(struct ShaderProgram *new_prg) {
    metal.shaderProgram = (struct ShaderProgramMetal *)new_prg;
}

void gfx_metal_remove_shaders(void) {
    for (int i = 0; i < MAX_FRAME_PASSES; i++) {
        for (int j = 0; j < CC_MAX_SHADERS; j++) {
            gfx_destroy_shader(metal.shaderProgramPool[i][j].vertexShader);
            gfx_destroy_shader(metal.shaderProgramPool[i][j].fragmentShader);
            metal.shaderProgramPool[i][j] = { 0 };
        }
        metal.shaderProgramPoolIndex[i] = 0;
        metal.shaderProgramPoolSize[i] = 0;
        metal.postProcessShaderProgramPool[i] = { 0 };
    }

    metal.shaderProgram = NULL;
    metal.lastShaderProgram = NULL;
}

struct ShaderProgram *gfx_metal_create_and_load_new_shader(struct ColorCombiner *cc) {
    CCFeatures cc_features = { 0 };
    gfx_cc_get_features(cc, &cc_features);

    struct Shader *vertexShader = (struct Shader *)calloc(1, sizeof(struct Shader));
    struct Shader *fragmentShader = (struct Shader *)calloc(1, sizeof(struct Shader));
    if (!vertexShader || !fragmentShader) {
        sys_fatal("Failed to allocate shaders, ran out of memory!");
    }

    gfx_generate_vertex_and_fragment_shader_from_cc(vertexShader, fragmentShader, cc, NULL, NULL);

    // get msl shader from spirv
    char *vs_msl = NULL;
    char *fs_msl = NULL;
    gfx_convert_spirv_to_msl(&vs_msl, vertexShader);
    gfx_convert_spirv_to_msl(&fs_msl, fragmentShader);

    // compile shader code to shader libraries
    MTL::Library *vsLibrary = metal_compile_source(vs_msl, "Vertex Shader");
    MTL::Library *fsLibrary = metal_compile_source(fs_msl, "Fragment Shader");

    free(vs_msl);
    free(fs_msl);

    // get entry point functions, entry point from spirv-cross shader is main0
    auto entryPointName = NS::String::string("main0", NS::UTF8StringEncoding);
    MTL::Function *vertFunc = vsLibrary->newFunction(entryPointName);
    MTL::Function *fragFunc = fsLibrary->newFunction(entryPointName);

    vsLibrary->release();
    fsLibrary->release();

    int framePassIndex = gCurrentFramePassIndex + 1;
    uint8_t poolIndex = metal.shaderProgramPoolIndex[framePassIndex];

    struct ShaderProgramMetal *prg = &metal.shaderProgramPool[framePassIndex][poolIndex];

    metal.shaderProgramPoolIndex[framePassIndex] = (poolIndex + 1) % CC_MAX_SHADERS;
    if (metal.shaderProgramPoolSize[framePassIndex] < CC_MAX_SHADERS) {
        metal.shaderProgramPoolSize[framePassIndex]++;
    }

    // generate render pipeline description
    auto pipelineDesc = MTL::RenderPipelineDescriptor::alloc()->init();
    pipelineDesc->setVertexFunction(vertFunc);
    pipelineDesc->setFragmentFunction(fragFunc);
    pipelineDesc->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float);

    // generate color attatchment description
    auto colorAttachment = pipelineDesc->colorAttachments()->object(0);
    colorAttachment->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    colorAttachment->setWriteMask(MTL::ColorWriteMaskAll);

    // enable or disable blending
    if (cc->cm.use_alpha) {
        colorAttachment->setBlendingEnabled(true);
        colorAttachment->setSourceRGBBlendFactor(MTL::BlendFactorSourceAlpha);
        colorAttachment->setDestinationRGBBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
        colorAttachment->setRgbBlendOperation(MTL::BlendOperationAdd);
        colorAttachment->setSourceAlphaBlendFactor(MTL::BlendFactorOne);
        colorAttachment->setDestinationAlphaBlendFactor(MTL::BlendFactorZero);
        colorAttachment->setAlphaBlendOperation(MTL::BlendOperationAdd);
    } else {
        colorAttachment->setBlendingEnabled(false);
    }

    // setup vbo
    auto vertexDesc = MTL::VertexDescriptor::vertexDescriptor();
    uint8_t iedIndex = 0;
    uint32_t currentOffset = 0;

    for (int i = 0; i < MAX_SHADER_INPUTS; i++) {
        if (gShaderInputs[i].size == 0) { continue; }

        int loc = vertexShader->shaderInputs[i].location;
        MTL::VertexFormat format = MTL::VertexFormatFloat4;

        switch (vertexShader->shaderInputs[i].size) {
            case 1: format = MTL::VertexFormatFloat; break;
            case 2: format = MTL::VertexFormatFloat2; break;
            case 3: format = MTL::VertexFormatFloat3; break;
            case 4: format = MTL::VertexFormatFloat4; break;
        }

        auto attribute = vertexDesc->attributes()->object(loc);
        attribute->setFormat(format);
        attribute->setOffset(currentOffset);
        attribute->setBufferIndex(1);

        currentOffset += vertexShader->shaderInputs[i].size * sizeof(float);
        iedIndex++;
    }

    if (iedIndex > 0) {
        auto layout = vertexDesc->layouts()->object(1);
        layout->setStride(currentOffset);
        layout->setStepFunction(MTL::VertexStepFunctionPerVertex);
        pipelineDesc->setVertexDescriptor(vertexDesc);
    }

    if (prg->pipeline != NULL) {
        prg->pipeline->release();
    }
    prg->pipeline = metal_create_pipeline(pipelineDesc);

    pipelineDesc->release();
    vertFunc->release();
    fragFunc->release();

    size_t numFloats = 0;
    for (int i = 0; i < MAX_SHADER_INPUTS; i++) {
        if (gShaderInputs[i].size == 0) { continue; }
        numFloats += gShaderInputs[i].size;
    }

    prg->hash = cc->hash;
    prg->numInputs = cc_features.num_inputs;
    prg->numFloats = numFloats;
    prg->usedTextures[0] = cc_features.used_textures[0];
    prg->usedTextures[1] = cc_features.used_textures[1];
    prg->usedLightmap = cc->cm.light_map;
    prg->usedFog = cc->cm.use_fog;

    if (prg->vertexShader) { free(prg->vertexShader); }
    if (prg->fragmentShader) { free(prg->fragmentShader); }

    prg->vertexShader = vertexShader;
    prg->fragmentShader = fragmentShader;
    prg->worldGeometry = cc->cm.world_geometry;

    if (prg->vertexConstantBuffer) { prg->vertexConstantBuffer->release(); }
    if (prg->fragmentConstantBuffer) { prg->fragmentConstantBuffer->release(); }
    if (prg->vertexUniformBuffer) { free(prg->vertexUniformBuffer); }
    if (prg->fragmentUniformBuffer) { free(prg->fragmentUniformBuffer); }

    generate_uniform_buffer_metal(prg, vertexShader, fragmentShader);

    metal.shaderProgram = prg;
    return (struct ShaderProgram *)prg;
}

struct ShaderProgram *gfx_metal_create_or_load_post_process_shader(void) {
    int framePassIndex = gCurrentFramePassIndex + 1;
    struct ShaderProgramMetal *prg = &metal.postProcessShaderProgramPool[framePassIndex];

    if (prg->pipeline != NULL) {
        metal.shaderProgram = prg;
        return (struct ShaderProgram *)prg;
    }

    struct Shader *vertexShader = (struct Shader *)calloc(1, sizeof(struct Shader));
    struct Shader *fragmentShader = (struct Shader *)calloc(1, sizeof(struct Shader));
    if (!vertexShader || !fragmentShader) {
        sys_fatal("Failed to allocate shaders, ran out of memory!");
    }

    gfx_generate_post_process_vertex_and_fragment_shader(vertexShader, fragmentShader, NULL, NULL);

    // get msl shader code
    char *vs_msl = NULL;
    char *fs_msl = NULL;
    gfx_convert_spirv_to_msl(&vs_msl, vertexShader);
    gfx_convert_spirv_to_msl(&fs_msl, fragmentShader);

    // compile vertex and fragment shader "libraries"
    MTL::Library *vsLibrary = metal_compile_source(vs_msl, "Vertex Shader");
    MTL::Library *fsLibrary = metal_compile_source(fs_msl, "Fragment Shader");

    free(vs_msl);
    free(fs_msl);

    // get entry point functions (spirv-cross entry func is main0)
    auto entryPointName = NS::String::string("main0", NS::UTF8StringEncoding);
    MTL::Function *vertFunc = vsLibrary->newFunction(entryPointName);
    MTL::Function *fragFunc = fsLibrary->newFunction(entryPointName);

    vsLibrary->release();
    fsLibrary->release();

    // setup pipeline
    auto pipelineDesc = MTL::RenderPipelineDescriptor::alloc()->init();
    pipelineDesc->setVertexFunction(vertFunc);
    pipelineDesc->setFragmentFunction(fragFunc);
    pipelineDesc->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float);

    // setup color attatchments
    auto colorAttachment = pipelineDesc->colorAttachments()->object(0);
    colorAttachment->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    colorAttachment->setBlendingEnabled(false);
    colorAttachment->setWriteMask(MTL::ColorWriteMaskAll);

    // get vbo layout setup
    auto vertexDesc = MTL::VertexDescriptor::vertexDescriptor();
    uint8_t iedIndex = 0;
    uint32_t currentOffset = 0;

    for (int i = 0; i < MAX_SHADER_INPUTS; i++) {
        if (gPostProcessShaderInputs[i].size == 0) { continue; }

        int loc = vertexShader->shaderInputs[i].location;
        MTL::VertexFormat format = MTL::VertexFormatFloat4;

        switch (vertexShader->shaderInputs[i].size) {
            case 1: format = MTL::VertexFormatFloat; break;
            case 2: format = MTL::VertexFormatFloat2; break;
            case 3: format = MTL::VertexFormatFloat3; break;
            case 4: format = MTL::VertexFormatFloat4; break;
        }

        auto attribute = vertexDesc->attributes()->object(loc);
        attribute->setFormat(format);
        attribute->setOffset(currentOffset);
        attribute->setBufferIndex(1);

        currentOffset += vertexShader->shaderInputs[i].size * sizeof(float);
        iedIndex++;
    }

    if (iedIndex > 0) {
        auto layout = vertexDesc->layouts()->object(1);
        layout->setStride(currentOffset);
        layout->setStepFunction(MTL::VertexStepFunctionPerVertex);
        pipelineDesc->setVertexDescriptor(vertexDesc);
    }

    // generate render pipeline using pipeline desc created above
    prg->pipeline = metal_create_pipeline(pipelineDesc);

    pipelineDesc->release();
    vertFunc->release();
    fragFunc->release();

    size_t numFloats = 0;
    for (int i = 0; i < MAX_SHADER_INPUTS; i++) {
        numFloats += gPostProcessShaderInputs[i].size;
    }

    prg->hash = framePassIndex;
    prg->numInputs = iedIndex;
    prg->numFloats = numFloats;
    prg->usedTextures[0] = true;
    prg->usedTextures[1] = false;
    prg->usedLightmap = false;
    prg->usedFog = false;
    prg->vertexShader = vertexShader;
    prg->fragmentShader = fragmentShader;
    prg->worldGeometry = false;

    generate_uniform_buffer_metal(prg, vertexShader, fragmentShader);

    metal.shaderProgram = prg;
    return (struct ShaderProgram *)prg;
}

static struct ShaderProgram *gfx_metal_lookup_shader(struct ColorCombiner* cc) {
    int framePassIndex = gCurrentFramePassIndex + 1;
    if (framePassIndex < 0 || framePassIndex >= MAX_FRAME_PASSES) { return NULL; }
    for (size_t i = 0; i < metal.shaderProgramPoolSize[framePassIndex]; i++) {
        if (metal.shaderProgramPool[framePassIndex][i].hash == cc->hash) {
            return (struct ShaderProgram *)&metal.shaderProgramPool[framePassIndex][i];
        }
    }
    return NULL;
}

static struct ShaderProgram *gfx_metal_lookup_shader_using_index(uint8_t shaderIndex, uint8_t framePassIndex) {
    framePassIndex++;
    if (shaderIndex >= metal.shaderProgramPoolSize[framePassIndex]) { return NULL; }
    return (struct ShaderProgram *)&metal.shaderProgramPool[framePassIndex][shaderIndex];
}

void gfx_metal_shader_get_info(struct ShaderProgram *prg, uint8_t *num_inputs, bool used_textures[2]) {
    struct ShaderProgramMetal *p = (struct ShaderProgramMetal *)prg;

    *num_inputs = p->numInputs;
    used_textures[0] = p->usedTextures[0];
    used_textures[1] = p->usedTextures[1];
}

void gfx_metal_create_framebuffer(struct FramePass *framePass) {
    if (!framePass) { return; }

    u32 viewportWidth;
    u32 viewportHeight;
    gfx_get_frame_pass_viewport_dimensions(framePass, &viewportWidth, &viewportHeight);

    auto colorDesc = MTL::TextureDescriptor::texture2DDescriptor(
        MTL::PixelFormatBGRA8Unorm,
        viewportWidth,
        viewportHeight,
        false
    );
    colorDesc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
    colorDesc->setStorageMode(MTL::StorageModePrivate);

    MTL::Texture *colorTex = metal.device->newTexture(colorDesc);
    colorDesc->release();

    if (!colorTex) {
        return;
    }

    auto depthDesc = MTL::TextureDescriptor::texture2DDescriptor(
        MTL::PixelFormatDepth32Float,
        viewportWidth,
        viewportHeight,
        false
    );
    depthDesc->setUsage(MTL::TextureUsageRenderTarget);
    depthDesc->setStorageMode(MTL::StorageModePrivate);

    MTL::Texture *depthTex = metal.device->newTexture(depthDesc);
    depthDesc->release();

    if (!depthTex) {
        colorTex->release();
        return;
    }

    framePass->passTexture = (uint64_t)colorTex;
    framePass->d3dRtv = (void *)colorTex;
    framePass->d3dDsv = (void *)depthTex;
    framePass->fbo = 1;
}

void gfx_metal_delete_framebuffer(struct FramePass *framePass) {
    if (!framePass || !framePass->fbo) return;

    if (framePass->d3dRtv) {
        ((MTL::Texture *)framePass->d3dRtv)->release();
        framePass->d3dRtv = NULL;
    }

    if (framePass->d3dDsv) {
        ((MTL::Texture *)framePass->d3dDsv)->release();
        framePass->d3dDsv = NULL;
    }

    framePass->passTexture = 0;
    framePass->fbo = 0;
}

void gfx_metal_set_framebuffer(struct FramePass *framePass) {
    if (!framePass || !framePass->fbo) return;

    // setup command buffer if necessary
    if (!metal.commandBuffer) {
        metal.drawable = metal.layer->nextDrawable();
        metal.commandBuffer = metal.commandQueue->commandBuffer();
        memset(metal.ringBufferOffset, 0, sizeof(metal.ringBufferOffset));
    }

    if (metal.encoder) {
        metal.encoder->endEncoding();
        metal.encoder = NULL;
    }

    // configure render pass descriptor
    MTL::RenderPassDescriptor *pass = MTL::RenderPassDescriptor::renderPassDescriptor();

    auto colorAttachment = pass->colorAttachments()->object(0);
    colorAttachment->setTexture((MTL::Texture *)framePass->d3dRtv);
    colorAttachment->setLoadAction(MTL::LoadActionClear);
    colorAttachment->setStoreAction(MTL::StoreActionStore);

    float r = framePass->clearColor[0] / 255.0f;
    float g = framePass->clearColor[1] / 255.0f;
    float b = framePass->clearColor[2] / 255.0f;
    float a = framePass->clearColor[3] / 255.0f;
    colorAttachment->setClearColor(MTL::ClearColor(r, g, b, a));

    auto depthAttachment = pass->depthAttachment();
    depthAttachment->setTexture((MTL::Texture *)framePass->d3dDsv);
    depthAttachment->setLoadAction(MTL::LoadActionClear);
    depthAttachment->setStoreAction(MTL::StoreActionDontCare);
    depthAttachment->setClearDepth(1.0);

    metal.encoder = metal.commandBuffer->renderCommandEncoder(pass);
    metal.encoder->setDepthStencilState(metal.depthStencilState);
    pass->release();

    metal.lastShaderProgram = NULL;
    metal.lastDepthTest = -1;
    metal.lastDepthMask = -1;
    metal.lastZModeDecal = -1;
    memset(metal.lastTextures, 0, sizeof(metal.lastTextures));
    memset(metal.lastSamplers, 0, sizeof(metal.lastSamplers));

    u32 viewportWidth;
    u32 viewportHeight;
    gfx_get_frame_pass_viewport_dimensions(framePass, &viewportWidth, &viewportHeight);

    MTL::Viewport vp;
    vp.originX = 0.0;
    vp.originY = 0.0;
    vp.width   = (double)viewportWidth;
    vp.height  = (double)viewportHeight;
    vp.znear   = 0.0;
    vp.zfar    = 1.0;
    metal.encoder->setViewport(vp);
}

void gfx_metal_reset_framebuffer(void) {
    // setup command buffer if necessary
    if (!metal.commandBuffer) {
        metal.drawable = metal.layer->nextDrawable();
        metal.commandBuffer = metal.commandQueue->commandBuffer();
        memset(metal.ringBufferOffset, 0, sizeof(metal.ringBufferOffset));
    }

    if (metal.encoder) {
        metal.encoder->endEncoding();
        metal.encoder = NULL;
    }

    MTL::RenderPassDescriptor *pass = MTL::RenderPassDescriptor::renderPassDescriptor();

    auto colorAttachment = pass->colorAttachments()->object(0);
    colorAttachment->setTexture(metal.drawable->texture());
    colorAttachment->setLoadAction(MTL::LoadActionLoad);
    colorAttachment->setStoreAction(MTL::StoreActionStore);

    auto depthAttachment = pass->depthAttachment();
    depthAttachment->setTexture(metal.depthTexture);
    depthAttachment->setLoadAction(MTL::LoadActionLoad);
    depthAttachment->setStoreAction(MTL::StoreActionDontCare);

    metal.encoder = metal.commandBuffer->renderCommandEncoder(pass);
    metal.encoder->setDepthStencilState(metal.depthStencilState);
    pass->release();

    metal.lastShaderProgram = NULL;
    metal.lastDepthTest = -1;
    metal.lastDepthMask = -1;
    metal.lastZModeDecal = -1;
    memset(metal.lastTextures, 0, sizeof(metal.lastTextures));
    memset(metal.lastSamplers, 0, sizeof(metal.lastSamplers));

    MTL::Viewport vp;
    vp.originX = 0.0;
    vp.originY = 0.0;
    vp.width   = (double)metal.currentWidth;
    vp.height  = (double)metal.currentHeight;
    vp.znear   = 0.0;
    vp.zfar    = 1.0;
    metal.encoder->setViewport(vp);
}

static void gfx_metal_set_uniform_for_specific_shader(struct ShaderProgramMetal *prg, struct Shader *shader, const char *name, ShaderUniformType type, const void *data, uint32_t numElements) {
    if (shader == NULL) { return; }

    size_t elementStride = 0;

    switch (type) {
        case SHADER_UNIFORM_TYPE_BOOL:
        case SHADER_UNIFORM_TYPE_INT:
        case SHADER_UNIFORM_TYPE_FLOAT: elementStride = sizeof(float); break;
        case SHADER_UNIFORM_TYPE_VEC2:  elementStride = sizeof(float) * 2; break;
        case SHADER_UNIFORM_TYPE_VEC3:  elementStride = sizeof(float) * 3; break;
        case SHADER_UNIFORM_TYPE_VEC4:  elementStride = sizeof(float) * 4; break;
        case SHADER_UNIFORM_TYPE_MAT4:  elementStride = sizeof(float) * 16; break;
    }

    size_t bytesNeeded = elementStride * numElements;

    for (int i = 0; i < MAX_SHADER_UNIFORMS; i++) {
        if (shader->shaderUniforms[i].size == 0) { break; }

        if (strcmp(shader->shaderUniforms[i].name, name) == 0) {
            int location = shader->shaderUniforms[i].location;
            int allowedSize = shader->shaderUniforms[i].size;

            size_t bytesToCopy = (bytesNeeded < (size_t)allowedSize) ? bytesNeeded : (size_t)allowedSize;

            if (shader->stage == GLSLANG_STAGE_VERTEX) {
                if (prg->vertexUniformBuffer != NULL) {
                    memcpy(&prg->vertexUniformBuffer[location], data, bytesToCopy);
                }
            } else {
                if (prg->fragmentUniformBuffer != NULL) {
                    memcpy(&prg->fragmentUniformBuffer[location], data, bytesToCopy);
                }
            }
            return;
        }
    }
}

void gfx_metal_set_uniform(struct ShaderProgram *prg, const char *name, ShaderUniformType type, const void *data, uint32_t numElements) {
    struct ShaderProgramMetal *metal_prg = (struct ShaderProgramMetal *)prg;
    if (metal_prg == NULL) {
        if (metal.shaderProgram == NULL) { return; }
        metal_prg = metal.shaderProgram;
    }

    gfx_metal_set_uniform_for_specific_shader(metal_prg, metal_prg->vertexShader, name, type, data, numElements);
    gfx_metal_set_uniform_for_specific_shader(metal_prg, metal_prg->fragmentShader, name, type, data, numElements);
}

uint32_t gfx_metal_new_texture(void) {
    metal.textures.resize(metal.textures.size() + 1);
    return (uint32_t)(metal.textures.size() - 1);
}

void gfx_metal_select_texture(int tile, uint32_t texture_id) {
    metal.currentTile = tile;
    metal.currentTextureIds[tile] = texture_id;
}

void gfx_metal_bind_texture_raw(int tile, uint64_t texture_id) {
    MTL::Texture *texture = (MTL::Texture *)texture_id;
    if (texture == NULL) { return; }

    metal.encoder->setFragmentTexture(texture, tile);
    MTL::SamplerState *sampler = (tile < 2) ? metal.textures[metal.currentTextureIds[tile]].sampler : metal.defaultSampler;
    metal.encoder->setFragmentSamplerState(sampler, tile);

    if (tile < 2) {
        metal.lastTextures[tile] = texture;
    }
}

void gfx_metal_upload_texture(const uint8_t *rgba32_buf, int width, int height) {
    auto desc = MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, (NS::UInteger)width, (NS::UInteger)height, false);

    desc->setUsage(MTL::TextureUsageShaderRead);
    desc->setStorageMode(MTL::StorageModeShared);

    MTL::Texture *texture = metal.device->newTexture(desc);
    desc->release();

    if (texture == NULL) {
        sys_fatal("Failed to allocate memory for Metal texture upload!");
    }

    MTL::Region region = MTL::Region::Make2D(0, 0, width, height);
    NS::UInteger bytesPerRow = (NS::UInteger)(width * 4);

    texture->replaceRegion(region, 0, rgba32_buf, bytesPerRow);

    struct TextureData *textureData = &metal.textures[metal.currentTextureIds[metal.currentTile]];
    textureData->width = width;
    textureData->height = height;

    if (textureData->texture != NULL) {
        textureData->texture->release();
    }

    textureData->texture = texture;
}

static MTL::SamplerAddressMode gfx_cm_to_metal(uint32_t val) {
    if (val & G_TX_CLAMP) {
        return MTL::SamplerAddressModeClampToEdge;
    }
    return (val & G_TX_MIRROR) ? MTL::SamplerAddressModeMirrorRepeat : MTL::SamplerAddressModeRepeat;
}

void gfx_metal_set_sampler_parameters(int tile, bool linear_filter, uint32_t cms, uint32_t cmt) {
    auto desc = MTL::SamplerDescriptor::alloc()->init();

    desc->setMinFilter(linear_filter ? MTL::SamplerMinMagFilterLinear : MTL::SamplerMinMagFilterNearest);
    desc->setMagFilter(linear_filter ? MTL::SamplerMinMagFilterLinear : MTL::SamplerMinMagFilterNearest);

    desc->setSAddressMode(gfx_cm_to_metal(cms));
    desc->setTAddressMode(gfx_cm_to_metal(cmt));
    desc->setRAddressMode(MTL::SamplerAddressModeRepeat);

    struct TextureData *textureData = &metal.textures[metal.currentTextureIds[tile]];
    textureData->linearFiltering = linear_filter;

    if (textureData->sampler != NULL) {
        textureData->sampler->release();
    }

    textureData->sampler = metal.device->newSamplerState(desc);
    desc->release();
}

void gfx_metal_set_depth_test(bool depth_test) {
    metal.depthTest = depth_test;
}

void gfx_metal_set_depth_mask(bool z_upd) {
    metal.depthMask = z_upd;
}

void gfx_metal_set_zmode_decal(bool zmode_decal) {
    metal.zModeDecal = zmode_decal;
}

void gfx_metal_set_viewport(int x, int y, int width, int height) {
    MTL::Viewport vp;
    vp.originX = x;
    vp.originY = y;
    vp.width   = width;
    vp.height  = height;
    vp.znear   = 0.0;
    vp.zfar    = 1.0;

    metal.encoder->setViewport(vp);
}

void gfx_metal_set_scissor(int x, int y, int width, int height) {
    MTL::ScissorRect r;
    r.x = x;
    r.y = metal.currentHeight - y - height;
    r.width = width;
    r.height = height;

    metal.encoder->setScissorRect(r);
}

void gfx_metal_set_use_alpha(bool use_alpha) {
    metal.useAlpha = use_alpha;
}

void gfx_metal_set_vsync(bool enabled) {
    if (metal.layer) {
        metal.layer->setDisplaySyncEnabled(enabled);
    }
}

void gfx_metal_draw_triangles(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris) {
    if (metal.shaderProgram == NULL) { return; }
    int framePassIndex = gCurrentFramePassIndex + 1;

    // handle depth and stencil changes
    if (metal.lastDepthTest != metal.depthTest || metal.lastDepthMask != metal.depthMask) {
        metal.lastDepthTest = metal.depthTest;
        metal.lastDepthMask = metal.depthMask;

        if (metal.depthStencilState != NULL) {
            metal.depthStencilState->release();
        }
        metal.depthStencilState = get_or_create_depth_stencil_state(metal.depthTest, metal.depthMask);
        metal.encoder->setDepthStencilState(metal.depthStencilState);
    }

    // update zmode decal and cull modes
    if (metal.lastZModeDecal != metal.zModeDecal) {
        metal.lastZModeDecal = metal.zModeDecal;

        if (metal.zModeDecal) {
            metal.encoder->setDepthBias(0.0f, -2.0f, 0.0f);
        } else {
            metal.encoder->setDepthBias(0.0f, 0.0f, 0.0f);
        }
    }

    metal.encoder->setCullMode(MTL::CullModeNone);
    metal.encoder->setFrontFacingWinding(MTL::WindingCounterClockwise);

    // bind texture data
    for (int i = 0; i < 2; i++) {
        if (metal.shaderProgram->usedTextures[i]) {
            struct TextureData &textureData = metal.textures[metal.currentTextureIds[i]];

            if (metal.lastTextures[i] != textureData.texture) {
                metal.lastTextures[i] = textureData.texture;
                metal.encoder->setFragmentTexture(textureData.texture, i);
            }

            if (metal.lastSamplers[i] != textureData.sampler) {
                metal.lastSamplers[i] = textureData.sampler;
                metal.encoder->setFragmentSamplerState(textureData.sampler, i);
            }

            if (metal.lastTextures[i] == textureData.texture || metal.lastSamplers[i] == textureData.sampler) {
                char sizeUniformName[MAX_SHADER_VARIABLE_NAME];
                snprintf(sizeUniformName, sizeof(sizeUniformName), "uTex%dSize", i);
                float texSize[2] = { (float)textureData.width, (float)textureData.height };
                gfx_metal_set_uniform((struct ShaderProgram *)metal.shaderProgram, sizeUniformName, SHADER_UNIFORM_TYPE_VEC2, texSize, 1);

                char filterUniformName[MAX_SHADER_VARIABLE_NAME];
                snprintf(filterUniformName, sizeof(filterUniformName), "uTex%dFilter", i);
                uint32_t isLinear = textureData.linearFiltering ? 1 : 0;
                gfx_metal_set_uniform((struct ShaderProgram *)metal.shaderProgram, filterUniformName, SHADER_UNIFORM_TYPE_INT, &isLinear, 1);
            }
        }
    }

    gfx_update_matrices();
    if (metal.shaderProgram->usedFog) {
        gfx_update_fog_uniforms();
    }
    smlua_call_event_hooks(HOOK_ON_SET_SHADER_UNIFORMS);

    // upload uniform data
    if (metal.shaderProgram->vertexUboSize > 0 && metal.shaderProgram->vertexUniformBuffer != NULL) {
        size_t size = metal.shaderProgram->vertexUboSize;
        size_t alignedOffset = (metal.ringBufferOffset[framePassIndex] + 15) & ~15;

        if (alignedOffset + size <= MAX_RING_BUFFER_SIZE) {
            uint8_t *dst = (uint8_t *)metal.dynamicRingBuffer[framePassIndex]->contents() + alignedOffset;
            memcpy(dst, metal.shaderProgram->vertexUniformBuffer, size);

            metal.encoder->setVertexBuffer(metal.dynamicRingBuffer[framePassIndex], alignedOffset, 0);
            metal.ringBufferOffset[framePassIndex] = alignedOffset + size;
        } else {
            // Todo: Make LOG_ERROR when possible
            printf("Metal: Ring buffer is full!\n");
        }
    }

    if (metal.shaderProgram->fragmentUboSize > 0 && metal.shaderProgram->fragmentUniformBuffer != NULL) {
        size_t size = metal.shaderProgram->fragmentUboSize;
        size_t alignedOffset = (metal.ringBufferOffset[framePassIndex] + 15) & ~15;

        if (alignedOffset + size <= MAX_RING_BUFFER_SIZE) {
            uint8_t *dst = (uint8_t *)metal.dynamicRingBuffer[framePassIndex]->contents() + alignedOffset;
            memcpy(dst, metal.shaderProgram->fragmentUniformBuffer, size);

            metal.encoder->setFragmentBuffer(metal.dynamicRingBuffer[framePassIndex], alignedOffset, 0);
            metal.ringBufferOffset[framePassIndex] = alignedOffset + size;
        } else {
            // Todo: Make LOG_ERROR when possible
            printf("Metal: Ring buffer is full!\n");
        }
    }

    if (buf_vbo_len > 0) {
        size_t size = buf_vbo_len * sizeof(float);
        size_t alignedOffset = (metal.ringBufferOffset[framePassIndex] + 15) & ~15;

        if (alignedOffset + size <= MAX_RING_BUFFER_SIZE) {
            uint8_t *dst = (uint8_t *)metal.dynamicRingBuffer[framePassIndex]->contents() + alignedOffset;
            memcpy(dst, buf_vbo, size);

            metal.encoder->setVertexBuffer(metal.dynamicRingBuffer[framePassIndex], alignedOffset, 1);
            metal.ringBufferOffset[framePassIndex] = alignedOffset + size;
        } else {
            // Todo: Make LOG_ERROR when possible
            printf("Metal: Ring buffer is full!\n");
        }
    }

    if (metal.lastShaderProgram != metal.shaderProgram) {
        metal.lastShaderProgram = metal.shaderProgram;
        metal.encoder->setRenderPipelineState(metal.shaderProgram->pipeline);
    }

    metal.encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, (NS::UInteger)0, (NS::UInteger)(buf_vbo_num_tris * 3));
}

void gfx_metal_init(void) {
    SDL_Window *wnd = gfx_sdl_get_window();
    metal.metalView = SDL_Metal_CreateView(wnd);
    metal.layer = static_cast<CA::MetalLayer *>(SDL_Metal_GetLayer(metal.metalView));

    // create and set device
    metal.device = MTL::CreateSystemDefaultDevice();
    if (!metal.device) {
        sys_fatal("Failed to create metal device!");
    }
    metal.layer->setDevice(metal.device);

    metal.layer->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    metal.layer->setFramebufferOnly(false);

    // get current window dimensions
    uint32_t w, h;
    gWindowApi->get_dimensions(&w, &h);

    metal.currentWidth = w;
    metal.currentHeight = h;

    metal.layer->setDrawableSize(CGSizeMake(w, h));

    // init command queue and vertex buffer
    metal.commandQueue = metal.device->newCommandQueue();
    if (!metal.commandQueue) {
        sys_fatal("Failed to create Metal command queue.");
    }

    metal.vertexBuffer = metal.device->newBuffer(VERTEX_STRIDE * sizeof(float), MTL::ResourceStorageModeShared);
    if (!metal.vertexBuffer) {
        sys_fatal("Failed to create Metal vertex buffer.");
    }

    for (int i = 0; i < MAX_FRAME_PASSES; i++) {
        metal.dynamicRingBuffer[i] = metal.device->newBuffer(MAX_RING_BUFFER_SIZE, MTL::ResourceStorageModeShared);
        if (!metal.dynamicRingBuffer[i]) {
            sys_fatal("Failed to create Metal dynamic ring buffer pool.");
        }
        metal.ringBufferOffset[i] = 0;
    }

    // setup depth stencil
    MTL::DepthStencilDescriptor *depthStencilDesc = MTL::DepthStencilDescriptor::alloc()->init();
    depthStencilDesc->setDepthCompareFunction(MTL::CompareFunctionLessEqual);
    depthStencilDesc->setDepthWriteEnabled(true);
    metal.depthStencilState = metal.device->newDepthStencilState(depthStencilDesc);
    depthStencilDesc->release();
    if (!metal.depthStencilState) {
        sys_fatal("Failed to create Metal depth stencil state.");
    }

    // setup default sampler
    MTL::SamplerDescriptor *samplerDesc = MTL::SamplerDescriptor::alloc()->init();
    samplerDesc->setMinFilter(MTL::SamplerMinMagFilterLinear);
    samplerDesc->setMagFilter(MTL::SamplerMinMagFilterLinear);
    samplerDesc->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
    samplerDesc->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
    samplerDesc->setRAddressMode(MTL::SamplerAddressModeClampToEdge);

    metal.defaultSampler = metal.device->newSamplerState(samplerDesc);
    samplerDesc->release();

    create_depth_texture();
}

void gfx_metal_on_resize(void) {
    for (int i = 0; i < MAX_CUSTOM_FRAME_PASSES; i++) {
        struct FramePass *framePass = &gFramePasses[i];
        if (!framePass->active) { continue; }

        if (framePass->width == 0 || framePass->height == 0) {
            // needs to be recreated to redo viewport size
            gfx_metal_delete_framebuffer(framePass);
        }
    }

    uint32_t w, h;
    gWindowApi->get_dimensions(&w, &h);

    metal.currentWidth = w;
    metal.currentHeight = h;

    metal.layer->setDrawableSize(CGSizeMake(w, h));

    create_depth_texture();
}

void gfx_metal_start_frame(void) {
    // setup command buffer
    if (!metal.commandBuffer) {
        metal.drawable = metal.layer->nextDrawable();
        metal.commandBuffer = metal.commandQueue->commandBuffer();
        memset(metal.ringBufferOffset, 0, sizeof(metal.ringBufferOffset));
    }

    // clear cache for frame
    metal.lastShaderProgram = NULL;
    metal.lastDepthTest = -1;
    metal.lastDepthMask = -1;
    metal.lastZModeDecal = -1;
    memset(metal.lastTextures, 0, sizeof(metal.lastTextures));
    memset(metal.lastSamplers, 0, sizeof(metal.lastSamplers));
}

void gfx_metal_end_frame(void) {
    if (metal.encoder) {
        metal.encoder->endEncoding();
        metal.encoder = NULL;
    }
}

void gfx_metal_finish_render(void) {
    if (metal.encoder) {
        metal.encoder->endEncoding();
        metal.encoder = NULL;
    }

    if (metal.commandBuffer) {
        if (metal.drawable) {
            metal.commandBuffer->presentDrawable(metal.drawable);
            metal.drawable = NULL;
        }
        metal.commandBuffer->commit();
        // TODO: Figure out what is causing the gpu to lose data, this shouldnt be here
        metal.commandBuffer->waitUntilCompleted();
        metal.commandBuffer = NULL;
    }
}

const char *gfx_metal_get_name(void) {
    return "Metal";
}

bool gfx_metal_is_legacy(void) {
    return false;
}

void gfx_metal_shutdown(void) {
}

struct GfxRenderingAPI gfx_metal_api = {
    gfx_metal_z_is_from_0_to_1,
    gfx_metal_unload_shader,
    gfx_metal_load_shader,
    gfx_metal_remove_shaders,
    gfx_metal_create_and_load_new_shader,
    gfx_metal_create_or_load_post_process_shader,
    gfx_metal_lookup_shader,
    gfx_metal_lookup_shader_using_index,
    gfx_metal_shader_get_info,
    gfx_metal_create_framebuffer,
    gfx_metal_delete_framebuffer,
    gfx_metal_set_framebuffer,
    gfx_metal_reset_framebuffer,
    gfx_metal_set_uniform,
    gfx_metal_new_texture,
    gfx_metal_select_texture,
    gfx_metal_bind_texture_raw,
    gfx_metal_upload_texture,
    gfx_metal_set_sampler_parameters,
    gfx_metal_set_depth_test,
    gfx_metal_set_depth_mask,
    gfx_metal_set_zmode_decal,
    gfx_metal_set_viewport,
    gfx_metal_set_scissor,
    gfx_metal_set_use_alpha,
    gfx_metal_set_vsync,
    gfx_metal_draw_triangles,
    gfx_metal_init,
    gfx_metal_on_resize,
    gfx_metal_start_frame,
    gfx_metal_end_frame,
    gfx_metal_finish_render,
    gfx_metal_get_name,
    gfx_metal_is_legacy,
    gfx_metal_shutdown
};

#endif
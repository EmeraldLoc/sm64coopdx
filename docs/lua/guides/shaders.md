# Shaders

# Please skip to [dev progress](#dev-progress)

## Resources

- [Default C Shader in Lua](../examples/shader-demo/default-shader.lua)
- [Example Vertex Shader](../examples/shader-demo/mirror-shader.lua)
- [Example Fragment Shader](../examples/shader-demo/invert-color-shader.lua)

## Before Starting...

You should have some basic knowledge on how writing mods in Lua works, and for more advanced shaders you should know how to work with shaders. But even if you are a beginner, you may find use in this guide.

When testing shaders, it is recommended to launch your game with the terminal. While technically the game will fallback on default shaders if a shader fails to compile, there are multiple cases that are common enough to where you will appreciate running the game via a terminal since the console will be unavailable/invisible.

## Types of Shaders

There are two types of shaders, post process shaders and "scene" shaders. Post process shaders are significantly easier than scene shaders, but scene shaders give you significantly more power. We will mainly be going over post process shaders in this guide.

The [default C scene shader converted to Lua can be found here](../examples/shader-demo/default-shader.lua). It does all of the boilerplate for you.

## Creating Shaders

Scene shaders can be created via the `HOOK_ON_VERTEX_SHADER_CREATE` and `HOOK_ON_FRAGMENT_SHADER_CREATE` hooks. These hooks provides a [ColorCombiner](../structs.md#ColorCombiner) and shader index, and the hooks expects you to return a shader back.

Post process shaders can be created via the `HOOK_ON_POST_PROCESS_VERTEX_SHADER_CREATE` and `HOOK_ON_POST_PROCESS_FRAGMENT_SHADER_CREATE` hooks. These hooks provide no parameters, and expect you to return a shader back.

The vertex shader allows for manipulating vertex data, whereas the fragment shader allows for manipulating color data. You do not have to provide both, but if you only use one, you are expected to send/receive data back as expected by their respective default shaders.

## Vertex Shaders

The bread and butter of vertex shaders are inputs. Inputs are given by the game's code and can be used for getting the vertex data. A list of inputs for shaders can be found [here](#Inputs). Something important about inputs is that a fragment shader cannot get inputs from C, so you need to pass inputs from the vertex shader as outputs for the fragment shader. An example of that would be the vertex position.

```lua
in vec4 aVtxPos;
out vec4 vVtxPos;
vVtxPos = aVtxPos;
```

The input `aVtxPos` is provided by C, and the output is created, which will be received in the fragment shader.

```lua
in vec4 vVtxPos;
```

The fragment shader takes in the vertex pos we exported in the vertex shader.

For a post process vertex shader example, we are going to mirror the entire world. First, grab the [default C post process shader converted to Lua](../examples/shader-demo/default-post-process-shader.lua), specifically the vertex portion.

*Note: While the format shown in the default shaders use `table.insert(xShader, "line")`, this is not at all required. You may use any method you'd like, including loading the shader from a different file. This method was picked for it's ease to inject any if statement checks anywhere you'd like that's outside the shader.*

```lua
gl_Position = aVtxPos;
```

This sets the vertex position in opengl to our vertex position provided by C. There are 4 main components to this position:

- `x` being the X coordinate
- `y` being the Y coordinate
- `z` being the Z coordinate
- `w` being the depth coordinate

Importantly, this position is not in world space, it is instead in clip space, but that does not matter for our example.

To get our mirror effect, we need to flip the `x` coordinate. So we need to create a vector 4 that flips our `x` component, and passes back our `y`, `z`, and `w` components as-is. This can be done via this syntax:

```lua
gl_Position = vec4(aVtxPos.x * -1.0, aVtxPos.yzw);
```

If you run the mod, you should now have a mirrored game! That's the basic rundown on vertex shaders! If something still isn't working, compare your code with [the example vertex shader](../examples/shader-demo/mirror-shader.lua) and try to figure out what you did wrong.

<img width="640" height="416" alt="Screenshot 2026-04-24 at 7 09 03 PM" src="https://github.com/user-attachments/assets/4218e701-1a71-4420-bf0f-fd8fff8baf81" />

*Note: Alternatively, to accomplish the same effect, you can flip the texture coordinate in the fragment shader.*

## Fragment Shaders

First, grab the [default C post process shader converted to Lua](../examples/shader-demo/default-post-process-shader.lua), specifically the fragment shader portion.

For our example fragment shader, we are going to be inverting the colors. Actually doing this is quite simple, but before we get to that, let's explain what we are working with:

First, unlike the vertex shader, you have to define the output manually in a fragment shader.

```lua
out vec4 fragColor;
```

In the fragment shader, we create an output for the fragment color. Unlike the vertex shader, an output does not go to another shader, instead, it goes to OpenGL to be used for color data.

All the outputs in the vertex shader can be read in the fragment shader as inputs. This allows data to be carried over from one to the other. What was `out` in the vertex shader becomes `in` in the fragment shader.

For our example, inverting the colors, it's quite simple.

```lua
fragColor = texture(uPassTex, vTexCoord);
```

Currently, this is what the frag color is set to. Instead of setting `fragColor` directly, we should first take the color outputted by the `texture` function and insert it into it's own variable.

```lua
vec4 texColor = texture(uPassTex, vTexCoord);
```

Then, inverting that color becomes trivial. When we set `fragColor`, all we need to do is invert `texColor.rgb` and preserve `texColor.a`. This can be done with the following syntax:

```lua
fragColor = vec4(1.0 - texColor.rgb, texColor.a);
```

And that's it! All colors in your game should now be completely inverted! That's the basic rundown on fragment shaders! If something still isn't working, compare your code with [the example fragment shader](../examples/shader-demo/invert-color-shader.lua) and try to figure out what you did wrong.

<img width="640" height="416" alt="Screenshot 2026-04-24 at 7 07 43 PM" src="https://github.com/user-attachments/assets/1b25a55d-082a-447d-8c75-a33ef143f2e2" />

## Uniforms

A shader may contain uniforms. As a naming convention, uniform variables typically start with a `u`, for instance, `uFrameCount`. There are multiple different uniforms already updated by C, but you can also update custom uniforms in Lua. First, here is a list of all the uniforms updated in C.

| Uniform Name | Type | Description |
| ---- | ---- | ---- |
| `uTex0` | `sampler2D` | The primary texture |
| `uTex1` | `sampler2D` | The secondary texture |
| `uTex0Size` | `vec2` | The width and height of the primary texture |
| `uTex1Size` | `vec2` | The width and height of the secondary texture |
| `uTex0Filter` | `bool` | True if the first texture is using linear filtering |
| `uTex1Filter` | `bool` | True if the second texture is using linear filtering |
| `uFilter` | `int` | The current global filtering mode (0 = Point, 1 = Linear, 2 = Three-point) |
| `uFrameCount` | `float` | A timer that increases every frame |
| `uLightmapColor` | `vec3` | The RGB color multiplier applied to the environment/lightmap |
| `uModelViewMatrix` | `mat4` | Both the model and view matrix. Multiply by the `uInverseCameraMatrix` to get the model matrix, and multiply by said model matrix to get the view matrix |
| `uProjectionMatrix` | `mat4` | Transforms view space to clip space, the inverse of the matrix transforms clip space to view space |
| `uInverseCameraMatrix` | `mat4` | The inverse of the camera matrix. Transforms view space to world space |
| `uXAdjustRatio` | `float` | For 16:9. This is the amount the X in clip space is adjusted by. When working with `uInverseCameraMatrix`, you should edit the clip space vector to undo the effects done in source |
| `uPassTex` | `sampler2D` | The texture from the previous pass |

For defining a custom uniform in lua, first, define the uniform and use the uniform as you intend in your shader code. Next you're going to want to store the shader index given by the hook, and if necessary the frame pass index as well. Create a table and store all your shader indexes. Then in lua, in any hook as seen fit, iterate through the list of shader indexes. Now, create a variable and set it to the returned value of `gfx_get_program_id_from_shader_index`. First, use that program with `gfx_use_program`, then get the uniform with `gfx_shader_get_uniform_location`. You should then set it with the appropriate `gfx_shader_set_` function. Here is an example:

```lua
local fogColor = { r = 168, g = 175, b = 195 }
-- iterate through the shader indexes
for _, index in pairs(sShaderIndexes) do
    -- get the program from the shader index
    local program = gfx_get_program_id_from_shader_index(index)
    -- use the program so we can set the uniforms
    gfx_use_program(program)

    -- get the uniform, which is a color of type vec4
    local uFogColor = gfx_shader_get_uniform_location(program, "uFogColor")
    -- set the vector 4 at that uniform location
    gfx_shader_set_vec4(uFogColor, fogColor.r / 255, fogColor.g / 255, fogColor.b / 255, 1.0)

    local uFogIntensity = gfx_shader_get_uniform_location(program, "uFogIntensity")
    gfx_shader_set_float(uFogIntensity, 5000.0)
end
```

That allows you to define your own uniforms and set your uniforms in Lua!

Lastly, if you have multiple shaders and find yourself frequently calling `gfx_reload_shaders`, you should cleanup the list of shader indexes on a shader refresh. Use the `HOOK_ON_REFRESH_SHADERS` hook to reset the shader indexes table, or do anything you need to when it comes to refreshing shaders.

```lua
hook_event(HOOK_ON_REFRESH_SHADERS, function ()
    sShaderIndexes = {}
end)
```

## Inputs

Inputs are passed into the vertex shader for further use. Here is a list of inputs provided by C.

### Scene Shader Inputs

| Input Name | Type | Description |
| ---- | ---- | ---- |
| `aVtxPos` | `vec4` | The vertex position in clip space (x, y, z, w) |
| `aTexCoord0` | `vec2` | The UV mapping for the primary texture |
| `aTexCoord1` | `vec2` | The UV mapping for the secondary texture |
| `aNormal` | `vec3` | The direction the surface is facing |
| `aFog` | `vec4` | Fog data provided by C |
| `aLightMap` | `vec2` | UV coordinates for a light map |
| `aInputX` | `vec4` | Color/alpha input from the Color Combiner, with X being the input number |
| `aBarycentric` | `vec3` | The barycentric coordinates of the vertex within its triangle |

### Post Process Shader Inputs

| Input Name | Type | Description |
| ---- | ---- | ---- |
| `aVtxPos` | `vec4` | The vertex position in clip space (x, y, z, w) |
| `aTexCoord` | `vec2` | The UV mapping for the pass texture |

## Dealing with the HUD and Skybox

TODO: Update for uniforms provided in shader presets

## Multipass Shaders

Multipass shaders allow you to draw the world multiple times in a single frame. It's where custom scene shaders can do the most work. Whether you're creating a minimap, adding lighting, wanting multiple cameras, and more, you'll want to use multipass shaders.

## Creating a frame pass

A frame pass can be created with `gfx_shader_create_frame_pass`. This function returns the frame pass index, which is anywhere from zero to the maximum number of frame passes. A frame pass may also be removed with `gfx_shader_remove_frame_pass`.

Frame passes can be configured with their configuration functions.

| Function | Description |
| -------- | ----------- |
| `gfx_shader_set_frame_pass_viewport` | Sets the viewport/resolution of the frame pass |
| `gfx_shader_set_frame_pass_draw_world` | Configures whether the frame pass should redraw the world or use a quad (quad uses post process shader, redrawing the world uses the scene shader) |

## Using frame passes

In your shader hooks, you can access which frame pass you are currently on with the `gfx_shader_get_current_frame_pass`. This is needed if you need to change your shader depending on the current frame pass.

Sometimes you may need to configure things before you redraw the world. For instance, on some shaders you may want to disable culling before you redraw the world. You can use `HOOK_BEFORE_DRAW_GEOMETRY` to achieve this. Check your current frame pass index using `gfx_shader_get_current_frame_pass`, and run code accordingly in this hook. This hook isn't unique to frame passes, it's called anytime the world geometry is about to be drawn.

## A Note on Matrices

The vertex position (`aVtxPos`) provided by C is in clip space. As shown in the [uniforms](#Uniforms) section, many matrices are provided by C. There are enough matrices to realistically get to any space you need.

## Limitations

- Realistically, no more than a single shader can be used at a time. This means that if 2 mods want to use their own shader, issues will occur.
- There are only so many inputs. While many are provided, there may still be some missing for your own shaders.
- DirectX is not supported.

While these limitations may improve in the future, this is where we are stuck for right now.

## Dev Progress

Good knowledge:

- NDC Z depth range is 0-1 on both opengl and directx, unlike before where it was -1 to 1 on opengl. This removes the need for seeing if the z pos is -1 to 1 or 0 to 1
- Fragment inputs MUST match vertex outputs in shaders or else inputs will not be set properly. That is not what opengl normally does because it is pretty lax, but for anything like dx11, and metal, it is much more strict

Current Bugs:

- Painting transition to bitdw doesnt work in dx11, opengl, or metal
- Particles only spawn in certain levels. Most likely cause is particles inheriting garbage data, have not looked into deeply
- Flickering in Metal
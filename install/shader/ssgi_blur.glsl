#version 460

#extension GL_GOOGLE_include_directive : enable
#extension GL_KHR_shader_subgroup_basic : enable
#extension GL_KHR_shader_subgroup_ballot : enable
#extension GL_EXT_samplerless_texture_functions : enable
#extension GL_KHR_shader_subgroup_quad : enable

#define SHARED_SAMPLER_SET    1

#include "common_shader.glsl"
#include "common_lighting.glsl"

layout (set = 0, binding = 0) uniform texture2D inDepth;
layout (set = 0, binding = 1) uniform texture2D inGbufferB;
layout (set = 0, binding = 2) uniform texture2D inSSGI;
layout (set = 0, binding = 3, rgba16f) uniform image2D imageFiltered;
layout (set = 0, binding = 4) uniform UniformFrameData { PerFrameData frameData; };

layout (set = 0, binding = 5) uniform texture2D inPrevGBufferB; // 
layout (set = 0, binding = 6) uniform texture2D inPrevDepth;

layout(push_constant) uniform PushConsts
{   
    int kStepSize;
};

float normalWeight(vec3 centerNormal, vec3 sampleNormal, float power)
{
    return pow(clamp(dot(centerNormal, sampleNormal), 0.0f, 1.0f), power);
}

float depthWeight(vec2 offset,float centeDepth, float sampleDepth, float phi)
{
#if 1
    // Cheap derivative of z make filter build brock.
    float ddx = subgroupQuadSwapHorizontal(centeDepth) - centeDepth;
    float ddy = subgroupQuadSwapVertical(centeDepth) - centeDepth;
    float zF = length(offset * vec2(ddx, ddy));
#else 
    float zF = 1.0;
#endif
    
    return exp(-abs(centeDepth - sampleDepth) / (zF * phi + 1e-10f));
}

float lumaWeight(float centerLuma, float sampleLuma, float phi)
{
    return abs(centerLuma - sampleLuma) / phi;
}

float edgeStoppingWeight(
    vec2 offset,
    float centerDepth,
    float sampleDepth,
    float phiZ,
    vec3 centerNormal,
    vec3 sampleNormal,
    float phiNormal,
    float centerLuma,
    float sampleLuma,
    float phiLuma)
{
    const float wZ  = depthWeight(offset, centerDepth, sampleDepth, phiZ);
    const float wNormal = normalWeight(centerNormal, sampleNormal, phiNormal);
    const float wL = lumaWeight(centerLuma, sampleLuma, phiLuma);

    return exp(0.0 - max(wL, 0.0) - max(wZ, 0.0)) * wNormal;
}

float variance3x3blur(ivec2 pos)
{
    float sum = 0.0f;
    const float kernel[2][2] = 
    {
        { 1.0 / 4.0, 1.0 / 8.0 },
        { 1.0 / 8.0, 1.0 / 16.0 }
    };

    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            ivec2 p = pos + ivec2(x, y);
            float k = kernel[abs(x)][abs(y)];
            sum += texelFetch(inSSGI, p, 0).a * k;
        }
    }

    return sum;
}

layout (local_size_x = 8, local_size_y = 8) in;
void main()
{
    ivec2 workSize = imageSize(imageFiltered);

    uvec2 groupThreadId = remap8x8(gl_LocalInvocationIndex);
    uvec2 dispatchId = groupThreadId + gl_WorkGroupID.xy * 8;
    ivec2 workPos = ivec2(dispatchId);

    if(workPos.x >= workSize.x || workPos.y >= workSize.y)
    {
        return;
    }

    const vec2 texelSize = 1.0f / vec2(workSize);
    const vec2 uv = (vec2(workPos) + vec2(0.5f)) * texelSize;

    // Skip sky background pixels.
    const float depth = texture(sampler2D(inDepth, pointClampEdgeSampler), uv).r;
    if(depth <= 0.0)
    {
        imageStore(imageFiltered, ivec2(workPos), vec4(0.0));
        return;
    }

    float sumFactor = 1.0;

    vec4 centerColor = texture(sampler2D(inSSGI, pointClampEdgeSampler), uv);
    vec4  sumColor   = centerColor;
    float centerLuma = dot(centerColor.xyz, vec3(1.0 / 3.0));

    vec3 centerNormal = unpackWorldNormal(texture(sampler2D(inGbufferB, pointClampEdgeSampler), uv).xyz);
    float centerDepth = linearizeDepth(depth, frameData);

    const float phiZ      = 1.0f;
    const float phiNormal = 32.0f;
    const float phiColor  = 30.0f;

    const float var = variance3x3blur(workPos);
    const float phiLuma = phiColor * sqrt(max(0.0, 1e-10 + var));

    const float kWeights[3] = { 1.0, 2.0 / 3.0, 1.0 / 6.0 };
    for(int y = -1; y < 2; y ++)
    {
        for(int x = -1; x < 2; x ++)
        {
            const ivec2 samplePos  = workPos + ivec2(x, y) * kStepSize;
            const vec2  sampleUv = (samplePos + vec2(0.5)) * texelSize;
            const float weight = kWeights[abs(x)] * kWeights[abs(y)];

            // Skip out of border pixels and center pixel.
            if(onRange(sampleUv, vec2(0.0), vec2(1.0)) && (x != 0 || y != 0))
            {
                vec4  sampleColor = texture(sampler2D(inSSGI, pointClampEdgeSampler), sampleUv);
                float sampleColorLuma = dot(sampleColor.xyz, vec3(1.0 / 3.0));

                float w = 1.0;
                {
                    vec3 sampleNormal = unpackWorldNormal(texture(sampler2D(inGbufferB, pointClampEdgeSampler), sampleUv).xyz);
                    float sampleDepth = linearizeDepth(texture(sampler2D(inDepth, pointClampEdgeSampler), sampleUv).r, frameData);

                    w = edgeStoppingWeight(
                        vec2(x, y),
                        centerDepth, 
                        sampleDepth,
                        phiZ,
                        centerNormal,
                        sampleNormal,
                        phiNormal,
                        centerLuma,
                        sampleColorLuma,
                        phiLuma);
                }

                float wFactor = w * weight;

                sumFactor += wFactor;
                sumColor  += vec4(vec3(wFactor), wFactor * wFactor) * sampleColor;
            }
        }
    }

    // Variance in w, normalize factor is differ from color.
    vec4 filteredColor = sumColor / vec4(vec3(sumFactor), sumFactor * sumFactor);
    imageStore(imageFiltered, ivec2(workPos), filteredColor);
}
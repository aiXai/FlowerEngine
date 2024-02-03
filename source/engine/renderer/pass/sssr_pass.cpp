#include "../deferred_renderer.h"
#include "../render_scene.h"
#include "../renderer.h"
#include "../scene_textures.h"

namespace engine
{
    struct SSSRPush
    {
        vec3  probe0Pos;
        float probe0ValidFactor;
        vec3  probe1Pos;
        float probe1ValidFactor;

        vec4  boxExtentData0;
        vec4  boxExtentData1;
        vec4  boxExtentData2;

        uint32_t samplesPerQuad;
        uint32_t temporalVarianceGuidedTracingEnabled;
        uint32_t mostDetailedMip = 0;

        // Max roughness stop to reflection sample.
        float roughnessThreshold; 
        float temporalVarianceThreshold;
    };

    struct GPUDispatchIndirectCommand
    {
        uint32_t x;
        uint32_t y;
        uint32_t z;
        uint32_t pad;
    };
    static_assert(sizeof(GPUDispatchIndirectCommand) % (4 * sizeof(float)) == 0);

    struct SSSRRayCounterSSBO
    {
        uint32_t rayCount;
        uint32_t denoiseTileCount;
    };

    class SSSRPass : public PassInterface
    {
    public:
        std::unique_ptr<ComputePipeResources> tileClassifyPipeline;
        std::unique_ptr<ComputePipeResources> argsPrepare;
        std::unique_ptr<ComputePipeResources> intersect;
        std::unique_ptr<ComputePipeResources> reproject;
        std::unique_ptr<ComputePipeResources> prefilter;
        std::unique_ptr<ComputePipeResources> temporal;
        std::unique_ptr<ComputePipeResources> apply;

        VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;

        // std::vector<VkDescriptorSet> m_sets;
    public:
        VkDescriptorSet allocateSet()
        {
            VkDescriptorSet set;
            {
                getContext()->descriptorFactoryBegin()
                    .bindNoInfo(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 0) // inHiz
                    .bindNoInfo(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1) // inDepth
                    .bindNoInfo(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 2) // inGbufferA
                    .bindNoInfo(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 3) // inGbufferB
                    .bindNoInfo(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4) // inGbufferS
                    .bindNoInfo(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 5) // in Velocity
                    .bindNoInfo(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 6) // inPrevDepth
                    .bindNoInfo(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 7) // inPrevGBufferB
                    .bindNoInfo(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 8) // inHDRSceneColor
                    .bindNoInfo(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 9) // inBRDFLut
                    .bindNoInfo(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10) // SSRRayCounterSSBO
                    .bindNoInfo(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 11) // SSRRayListSSBO
                    .bindNoInfo(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 12) // SSRDenoiseTileListSSBO
                    .bindNoInfo(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 13) // HDRSceneColorImage
                    .bindNoInfo(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 14) // inCubeGlobalPrefilter
                    .bindNoInfo(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 15) // inGTAO
                    .bindNoInfo(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 16) // SSRIntersectCmdSSBO
                    .bindNoInfo(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 17) // SSRDenoiseCmdSSBO
                    .bindNoInfo(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 18) // SSRExtractRoughness
                    .bindNoInfo(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 19) // inSSRExtractRoughness
                    .bindNoInfo(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 20) // SSRIntersection
                    .bindNoInfo(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 21) // inSSRIntersection
                    .bindNoInfo(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 22) // SSR prev frame roughness
                    .bindNoInfo(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 23) // SSR prev frame radiance
                    .bindNoInfo(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 24) // SSR prev frame sample count
                    .bindNoInfo(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 25) // SSR reproject radiance
                    .bindNoInfo(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 26) // SSR average radiance
                    .bindNoInfo(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 27) // SSR variance
                    .bindNoInfo(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 28) // SSR sample count
                    .bindNoInfo(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 29) // in SSR reproject radiance
                    .bindNoInfo(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 30) // in SSR average radiance
                    .bindNoInfo(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 31) // in SSR variance
                    .bindNoInfo(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 32) // in SSR variance history
                    .bindNoInfo(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 33) // SSR prefilter radiance
                    .bindNoInfo(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 34) // SSR prefilter variance
                    .bindNoInfo(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 35) // in SSR prefilter radiance
                    .bindNoInfo(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 36) // in SSR prefilter variance
                    .bindNoInfo(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 37) // SSR temporal radiance
                    .bindNoInfo(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 38) // SSR temporal variance
                    .bindNoInfo(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 39) // Uniform buffer
                    .bindNoInfo(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 40)
                    .bindNoInfo(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 41)
                    .buildNoInfo(setLayout, set);
            }
            return set;
        }

        virtual void onInit() override
        {
            // m_sets.resize(getContext()->getSwapchain().getBackbufferCount());
            allocateSet();

            // for (size_t i = 0; i < m_sets.size(); i++)

            std::vector<VkDescriptorSetLayout> setLayouts = {
                setLayout,
                getContext()->getSamplerCache().getCommonDescriptorSetLayout(),
                getRenderer()->getBlueNoise().spp_1_buffer.setLayouts
            };
            uint32_t pushSize = (uint32_t)sizeof(SSSRPush);

            tileClassifyPipeline = std::make_unique<ComputePipeResources>("shader/sssr_tile.glsl", pushSize, setLayouts);
            argsPrepare = std::make_unique<ComputePipeResources>("shader/sssr_intersect_args.glsl", pushSize, setLayouts);
            intersect = std::make_unique<ComputePipeResources>("shader/sssr_intersect.glsl", pushSize, setLayouts);
            reproject = std::make_unique<ComputePipeResources>("shader/sssr_reproject.glsl", pushSize, setLayouts);
            prefilter = std::make_unique<ComputePipeResources>("shader/sssr_prefilter.glsl", pushSize, setLayouts);
            temporal = std::make_unique<ComputePipeResources>("shader/sssr_temporal.glsl", pushSize, setLayouts);
            apply = std::make_unique<ComputePipeResources>("shader/sssr_apply.glsl", pushSize, setLayouts);
        }

        virtual void release() override
        {
            tileClassifyPipeline.reset();
            argsPrepare.reset();
            intersect.reset();
            reproject.reset();
            prefilter.reset();
            temporal.reset();
            apply.reset();
        }
    };

    void DeferredRenderer::renderSSSR(
        VkCommandBuffer cmd,
        GBufferTextures* inGBuffers,
        RenderScene* scene,
        BufferParameterHandle perFrameGPU,
        PoolImageSharedRef inHiz,
        PoolImageSharedRef inSSAOBentNormal,
        const SkyLightRenderContext& inSky,
        ReflectionProbeContext& reflectionProbeContext)
    {
        if (!inSky.skylightReflection || !m_history.prevDepth || !m_history.prevGBufferB || !m_history.prevHdrBeforeAA)
        {
            return;
        }

        auto* pass = getContext()->getPasses().get<SSSRPass>();
        auto* rtPool = &getContext()->getRenderTargetPools();

        auto& hdrSceneColor = inGBuffers->hdrSceneColor->getImage();
        auto& gbufferA = inGBuffers->gbufferA->getImage();
        auto& gbufferB = inGBuffers->gbufferB->getImage();
        auto& gbufferS = inGBuffers->gbufferS->getImage();
        auto& gbufferV = inGBuffers->gbufferV->getImage();
        auto& sceneDepthZ = inGBuffers->depthTexture->getImage();
        auto& skyPrefilterImage = inSky.skylightReflection->getImage();

        hdrSceneColor.transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, buildBasicImageSubresource());
        gbufferA.transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, buildBasicImageSubresource());
        gbufferB.transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, buildBasicImageSubresource());
        gbufferS.transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, buildBasicImageSubresource());
        gbufferV.transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, buildBasicImageSubresource());
        sceneDepthZ.transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, RHIDefaultImageSubresourceRange(VK_IMAGE_ASPECT_DEPTH_BIT));
        skyPrefilterImage.transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, buildBasicImageSubresourceCube());
        inHiz->getImage().transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, buildBasicImageSubresource());

        VkDescriptorImageInfo hizInfo = RHIDescriptorImageInfoSample(inHiz->getImage().getOrCreateView(buildBasicImageSubresource()).view);
        VkDescriptorImageInfo depthInfo = RHIDescriptorImageInfoSample(sceneDepthZ.getOrCreateView(RHIDefaultImageSubresourceRange(VK_IMAGE_ASPECT_DEPTH_BIT)).view);
        VkDescriptorImageInfo gbufferAInfo = RHIDescriptorImageInfoSample(gbufferA.getOrCreateView(buildBasicImageSubresource()).view);
        VkDescriptorImageInfo gbufferBInfo = RHIDescriptorImageInfoSample(gbufferB.getOrCreateView(buildBasicImageSubresource()).view);
        VkDescriptorImageInfo gbufferSInfo = RHIDescriptorImageInfoSample(gbufferS.getOrCreateView(buildBasicImageSubresource()).view);
        VkDescriptorImageInfo gbufferVInfo = RHIDescriptorImageInfoSample(gbufferV.getOrCreateView(buildBasicImageSubresource()).view);
        VkDescriptorImageInfo brdfLutInfo = RHIDescriptorImageInfoSample(getRenderer()->getSharedTextures().brdfLut->getOrCreateView(buildBasicImageSubresource()).view);
        VkDescriptorImageInfo prevDepthInfo = RHIDescriptorImageInfoSample(m_history.prevDepth->getImage().getOrCreateView(RHIDefaultImageSubresourceRange(VK_IMAGE_ASPECT_DEPTH_BIT)).view);

        VkDescriptorImageInfo preGBufferBInfo = RHIDescriptorImageInfoSample(m_history.prevGBufferB->getImage().getOrCreateView(buildBasicImageSubresource()).view);

        VkImageView globalPrefilterView = skyPrefilterImage.getOrCreateView(buildBasicImageSubresourceCube(), VK_IMAGE_VIEW_TYPE_CUBE).view;
        VkDescriptorImageInfo globalPrefilterInfo = RHIDescriptorImageInfoSample(globalPrefilterView);

        VkDescriptorImageInfo probe0Info = RHIDescriptorImageInfoSample(globalPrefilterView);
        VkDescriptorImageInfo probe1Info = RHIDescriptorImageInfoSample(globalPrefilterView);

        if (reflectionProbeContext.probe0)
        {
            probe0Info = RHIDescriptorImageInfoSample(reflectionProbeContext.probe0->getImage().getOrCreateView(buildBasicImageSubresourceCube(), VK_IMAGE_VIEW_TYPE_CUBE).view);
        }
        if (reflectionProbeContext.probe1)
        {
            probe1Info = RHIDescriptorImageInfoSample(reflectionProbeContext.probe1->getImage().getOrCreateView(buildBasicImageSubresourceCube(), VK_IMAGE_VIEW_TYPE_CUBE).view);
        }

        VkDescriptorImageInfo hdrImageInfo = RHIDescriptorImageInfoStorage(hdrSceneColor.getOrCreateView(buildBasicImageSubresource()).view);
        VkDescriptorImageInfo hdrSampleInfo = RHIDescriptorImageInfoSample(m_history.prevHdrBeforeAA->getImage().getOrCreateView(buildBasicImageSubresource()).view);

        // Prepare rts.
        auto updateRts = [&]()
        {
            bool bShouldRebuild = false;
            uint32_t width = sceneDepthZ.getExtent().width;
            uint32_t height = sceneDepthZ.getExtent().height;

            if (m_history.sssrResources.rt_ssrReproject == nullptr)
            {
                bShouldRebuild = true;
            }
            else if (m_history.sssrResources.rt_ssrReproject->getImage().getExtent().width != width || m_history.sssrResources.rt_ssrReproject->getImage().getExtent().height != height)
            {
                bShouldRebuild = true;
            }

            if (!bShouldRebuild)
            {
                return;
            }

            m_history.sssrResources.rt_ssrRadiance = rtPool->createPoolImage(
                "SSR Radiance 0",
                width,
                height,
                VK_FORMAT_R16G16B16A16_SFLOAT,
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

            m_history.sssrResources.rt_ssrPrevRadiance = rtPool->createPoolImage(
                "SSR Radiance 0",
                width,
                height,
                VK_FORMAT_R16G16B16A16_SFLOAT,
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

            m_history.sssrResources.rt_ssrReproject = rtPool->createPoolImage(
                "SSR reproject",
                width,
                height,
                VK_FORMAT_R16G16B16A16_SFLOAT,
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);


            m_history.sssrResources.rt_ssrAverageRadiance = rtPool->createPoolImage(
                "SSR Average Radiance",
                divideRoundingUp(width, 8u),
                divideRoundingUp(height, 8u),
                VK_FORMAT_B10G11R11_UFLOAT_PACK32,
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

            m_history.sssrResources.rt_ssrRoughness = rtPool->createPoolImage(
                "SSRExtractRoughness 0",
                width,
                height,
                VK_FORMAT_R8_UNORM,
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

            m_history.sssrResources.rt_ssrPrevRoughness = rtPool->createPoolImage(
                "SSRExtractRoughness - 1",
                width,
                height,
                VK_FORMAT_R8_UNORM,
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

            m_history.sssrResources.rt_ssrVariance = rtPool->createPoolImage(
                "SSR Variance 0",
                width,
                height,
                VK_FORMAT_R16_SFLOAT,
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

            m_history.sssrResources.rt_ssrPrevVariance = rtPool->createPoolImage(
                "SSR Variance - 1",
                width,
                height,
                VK_FORMAT_R16_SFLOAT,
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

            m_history.sssrResources.rt_ssrSampleCount = rtPool->createPoolImage(
                "SSR SampleCount",
                width,
                height,
                VK_FORMAT_R16_SFLOAT,
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

            m_history.sssrResources.rt_ssrPrevSampleCount = rtPool->createPoolImage(
                "SSR SampleCount - 1",
                width,
                height,
                VK_FORMAT_R16_SFLOAT,
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

            m_history.sssrResources.rt_ssrRadiance->getImage().transitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL, buildBasicImageSubresource());
            m_history.sssrResources.rt_ssrPrevRadiance->getImage().transitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL, buildBasicImageSubresource());
            m_history.sssrResources.rt_ssrReproject->getImage().transitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL, buildBasicImageSubresource());
            m_history.sssrResources.rt_ssrAverageRadiance->getImage().transitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL, buildBasicImageSubresource());
            m_history.sssrResources.rt_ssrPrevRoughness->getImage().transitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL, buildBasicImageSubresource());
            m_history.sssrResources.rt_ssrRoughness->getImage().transitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL, buildBasicImageSubresource());
            m_history.sssrResources.rt_ssrVariance->getImage().transitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL, buildBasicImageSubresource());
            m_history.sssrResources.rt_ssrPrevVariance->getImage().transitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL, buildBasicImageSubresource());
            m_history.sssrResources.rt_ssrSampleCount->getImage().transitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL, buildBasicImageSubresource());
            m_history.sssrResources.rt_ssrPrevSampleCount->getImage().transitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL, buildBasicImageSubresource());

            VkClearColorValue clearValue = {};
            clearValue.float32[0] = 0;
            clearValue.float32[1] = 0;
            clearValue.float32[2] = 0;
            clearValue.float32[3] = 0;

            VkImageSubresourceRange subresourceRange = buildBasicImageSubresource();

            // Initial resource clears
            vkCmdClearColorImage(cmd, m_history.sssrResources.rt_ssrRadiance->getImage().getImage(), VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &subresourceRange);
            vkCmdClearColorImage(cmd, m_history.sssrResources.rt_ssrPrevRadiance->getImage().getImage(), VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &subresourceRange);
            vkCmdClearColorImage(cmd, m_history.sssrResources.rt_ssrReproject->getImage().getImage(), VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &subresourceRange);
            vkCmdClearColorImage(cmd, m_history.sssrResources.rt_ssrAverageRadiance->getImage().getImage(), VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &subresourceRange);
            vkCmdClearColorImage(cmd, m_history.sssrResources.rt_ssrPrevRoughness->getImage().getImage(), VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &subresourceRange);
            vkCmdClearColorImage(cmd, m_history.sssrResources.rt_ssrRoughness->getImage().getImage(), VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &subresourceRange);
            vkCmdClearColorImage(cmd, m_history.sssrResources.rt_ssrVariance->getImage().getImage(), VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &subresourceRange);
            vkCmdClearColorImage(cmd, m_history.sssrResources.rt_ssrPrevVariance->getImage().getImage(), VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &subresourceRange);
            vkCmdClearColorImage(cmd, m_history.sssrResources.rt_ssrSampleCount->getImage().getImage(), VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &subresourceRange);
            vkCmdClearColorImage(cmd, m_history.sssrResources.rt_ssrPrevSampleCount->getImage().getImage(), VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &subresourceRange);


            m_history.sssrResources.rt_ssrRadiance->getImage().transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, buildBasicImageSubresource());
            m_history.sssrResources.rt_ssrPrevRadiance->getImage().transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, buildBasicImageSubresource());
            m_history.sssrResources.rt_ssrReproject->getImage().transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, buildBasicImageSubresource());
            m_history.sssrResources.rt_ssrAverageRadiance->getImage().transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, buildBasicImageSubresource());
            m_history.sssrResources.rt_ssrPrevRoughness->getImage().transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, buildBasicImageSubresource());
            m_history.sssrResources.rt_ssrRoughness->getImage().transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, buildBasicImageSubresource());
            m_history.sssrResources.rt_ssrVariance->getImage().transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, buildBasicImageSubresource());
            m_history.sssrResources.rt_ssrPrevVariance->getImage().transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, buildBasicImageSubresource());
            m_history.sssrResources.rt_ssrSampleCount->getImage().transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, buildBasicImageSubresource());
            m_history.sssrResources.rt_ssrPrevSampleCount->getImage().transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, buildBasicImageSubresource());
        };
        updateRts();

        VkDescriptorImageInfo ssrReprojectImageInfo = RHIDescriptorImageInfoStorage(m_history.sssrResources.rt_ssrReproject->getImage().getOrCreateView(buildBasicImageSubresource()).view);
        VkDescriptorImageInfo ssrReprojectInfo = RHIDescriptorImageInfoSample(m_history.sssrResources.rt_ssrReproject->getImage().getOrCreateView(buildBasicImageSubresource()).view);
        VkDescriptorImageInfo ssrVarianceImageInfo = RHIDescriptorImageInfoStorage(m_history.sssrResources.rt_ssrVariance->getImage().getOrCreateView(buildBasicImageSubresource()).view);
        VkDescriptorImageInfo ssrVarianceInfo = RHIDescriptorImageInfoSample(m_history.sssrResources.rt_ssrVariance->getImage().getOrCreateView(buildBasicImageSubresource()).view);
        VkDescriptorImageInfo ssrVarianceHistoryImageInfo = RHIDescriptorImageInfoStorage(m_history.sssrResources.rt_ssrPrevVariance->getImage().getOrCreateView(buildBasicImageSubresource()).view);
        VkDescriptorImageInfo ssrVarianceHistoryInfo = RHIDescriptorImageInfoSample(m_history.sssrResources.rt_ssrPrevVariance->getImage().getOrCreateView(buildBasicImageSubresource()).view);
        VkDescriptorImageInfo ssrSampleCountImageInfo = RHIDescriptorImageInfoStorage(m_history.sssrResources.rt_ssrSampleCount->getImage().getOrCreateView(buildBasicImageSubresource()).view);
        VkDescriptorImageInfo ssrSampleCountHistoryInfo = RHIDescriptorImageInfoSample(m_history.sssrResources.rt_ssrPrevSampleCount->getImage().getOrCreateView(buildBasicImageSubresource()).view);
        VkDescriptorImageInfo ssrIntersectImageInfo = RHIDescriptorImageInfoStorage(m_history.sssrResources.rt_ssrRadiance->getImage().getOrCreateView(buildBasicImageSubresource()).view);
        VkDescriptorImageInfo ssrIntersectInfo = RHIDescriptorImageInfoSample(m_history.sssrResources.rt_ssrRadiance->getImage().getOrCreateView(buildBasicImageSubresource()).view);
        VkDescriptorImageInfo ssrIntersectHistoryImageInfo = RHIDescriptorImageInfoStorage(m_history.sssrResources.rt_ssrPrevRadiance->getImage().getOrCreateView(buildBasicImageSubresource()).view);
        VkDescriptorImageInfo ssrIntersectHistoryInfo = RHIDescriptorImageInfoSample(m_history.sssrResources.rt_ssrPrevRadiance->getImage().getOrCreateView(buildBasicImageSubresource()).view);
        VkDescriptorImageInfo ssrAverageImageInfo = RHIDescriptorImageInfoStorage(m_history.sssrResources.rt_ssrAverageRadiance->getImage().getOrCreateView(buildBasicImageSubresource()).view);
        VkDescriptorImageInfo ssrAverageInfo = RHIDescriptorImageInfoSample(m_history.sssrResources.rt_ssrAverageRadiance->getImage().getOrCreateView(buildBasicImageSubresource()).view);
        VkDescriptorImageInfo roughnessExtractHistoryInfo = RHIDescriptorImageInfoSample(m_history.sssrResources.rt_ssrPrevRoughness->getImage().getOrCreateView(buildBasicImageSubresource()).view);
        VkDescriptorImageInfo roughnessExtractImageInfo = RHIDescriptorImageInfoStorage(m_history.sssrResources.rt_ssrRoughness->getImage().getOrCreateView(buildBasicImageSubresource()).view);
        VkDescriptorImageInfo roughnessExtractInfo = RHIDescriptorImageInfoSample(m_history.sssrResources.rt_ssrRoughness->getImage().getOrCreateView(buildBasicImageSubresource()).view);

        const uint32_t maxRayCount = sceneDepthZ.getExtent().width * sceneDepthZ.getExtent().height; // Max case is one pixel one ray.
        const uint32_t maxDenoiseListCount = maxRayCount / (8 * 8) + 1; // Tile run in 8x8.

        auto ssboCounterBuffer = getContext()->getBufferParameters().getStaticStorage("ssrCounterBuffer", sizeof(SSSRRayCounterSSBO));
        auto ssboRayListBuffer = getContext()->getBufferParameters().getStaticStorage("ssrRayListBuffer", sizeof(uint32_t) * maxRayCount);
        auto ssboDenoiseListBuffer = getContext()->getBufferParameters().getStaticStorage("ssrDenoiseListBuffer", sizeof(uint32_t) * maxDenoiseListCount);
        auto ssboIntersectCmdBuffer = getContext()->getBufferParameters().getIndirectStorage("ssboIntersectCmdBuffer", sizeof(GPUDispatchIndirectCommand));
        auto ssboDenoiseCmdBuffer = getContext()->getBufferParameters().getIndirectStorage("ssboDenoiseCmdBuffer", sizeof(GPUDispatchIndirectCommand));


        // Clear count buffer. list buffer don't care and it will update by thread.
        vkCmdFillBuffer(cmd, *ssboCounterBuffer->getBuffer(), 0, ssboCounterBuffer->getBuffer()->getSize(), 0u);
        std::array<VkBufferMemoryBarrier2, 1> fillBarriers
        {
            RHIBufferBarrier(ssboCounterBuffer->getBuffer()->getVkBuffer(),
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT),
        };
        RHIPipelineBarrier(cmd, 0, (uint32_t)fillBarriers.size(), fillBarriers.data(), 0, nullptr);

        VkDescriptorBufferInfo ssboCounterBufferInfo = ssboCounterBuffer->getBufferInfo();
        VkDescriptorBufferInfo ssboRayListBufferInfo = ssboRayListBuffer->getBufferInfo();
        VkDescriptorBufferInfo ssboDenoiseListBufferInfo = ssboDenoiseListBuffer->getBufferInfo();
        VkDescriptorBufferInfo ssboArgsIntersectInfo = ssboIntersectCmdBuffer->getBufferInfo();
        VkDescriptorBufferInfo ssboArgsDenoiseInfo = ssboDenoiseCmdBuffer->getBufferInfo();
        VkDescriptorImageInfo gtaoInfo = RHIDescriptorImageInfoSample(inSSAOBentNormal->getImage().getOrCreateView(buildBasicImageSubresource()).view);
        VkDescriptorBufferInfo frameBufferInfo = perFrameGPU->getBufferInfo();
        std::vector<VkWriteDescriptorSet> writes
        {
            RHIPushWriteDescriptorSetImage(0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &hizInfo),
            RHIPushWriteDescriptorSetImage(1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &depthInfo),
            RHIPushWriteDescriptorSetImage(2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &gbufferAInfo),
            RHIPushWriteDescriptorSetImage(3, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &gbufferBInfo),
            RHIPushWriteDescriptorSetImage(4, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &gbufferSInfo),
            RHIPushWriteDescriptorSetImage(5, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &gbufferVInfo),
            RHIPushWriteDescriptorSetImage(6, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  &prevDepthInfo),
            RHIPushWriteDescriptorSetImage(7, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,   &preGBufferBInfo),
            RHIPushWriteDescriptorSetImage(8, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,   &hdrSampleInfo),
            RHIPushWriteDescriptorSetImage(9, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,   &brdfLutInfo),
            RHIPushWriteDescriptorSetBuffer(10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &ssboCounterBufferInfo),
            RHIPushWriteDescriptorSetBuffer(11, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &ssboRayListBufferInfo),
            RHIPushWriteDescriptorSetBuffer(12, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &ssboDenoiseListBufferInfo),
            RHIPushWriteDescriptorSetImage(13, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &hdrImageInfo),
            RHIPushWriteDescriptorSetImage(14, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &globalPrefilterInfo),
            RHIPushWriteDescriptorSetImage(15, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &gtaoInfo),
            RHIPushWriteDescriptorSetBuffer(16, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &ssboArgsIntersectInfo),
            RHIPushWriteDescriptorSetBuffer(17, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &ssboArgsDenoiseInfo),
            RHIPushWriteDescriptorSetImage(18, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &roughnessExtractImageInfo), // extract roughness
            RHIPushWriteDescriptorSetImage(19, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &roughnessExtractInfo),
            RHIPushWriteDescriptorSetImage(20, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &ssrIntersectImageInfo), // ssr intersect result
            RHIPushWriteDescriptorSetImage(21, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &ssrIntersectInfo),
            RHIPushWriteDescriptorSetImage(22, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &roughnessExtractHistoryInfo), // inPrevSSRExtractRoughness
            RHIPushWriteDescriptorSetImage(23, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &ssrIntersectHistoryInfo), // inPrevSSRRadiance
            RHIPushWriteDescriptorSetImage(24, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &ssrSampleCountHistoryInfo), // inPrevSampleCount
            RHIPushWriteDescriptorSetImage(25, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &ssrReprojectImageInfo), // ssr reproject
            RHIPushWriteDescriptorSetImage(26, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &ssrAverageImageInfo), // ssr average
            RHIPushWriteDescriptorSetImage(27, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &ssrVarianceImageInfo), // ssr variance
            RHIPushWriteDescriptorSetImage(28, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &ssrSampleCountImageInfo), // ssr sample count
            RHIPushWriteDescriptorSetImage(29, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &ssrReprojectInfo),
            RHIPushWriteDescriptorSetImage(30, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &ssrAverageInfo),
            RHIPushWriteDescriptorSetImage(31, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &ssrVarianceInfo),
            RHIPushWriteDescriptorSetImage(32, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &ssrVarianceHistoryInfo), // ssr variance history
            RHIPushWriteDescriptorSetImage(33, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &ssrIntersectHistoryImageInfo), // ssr prefilter radiance.
            RHIPushWriteDescriptorSetImage(34, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &ssrVarianceHistoryImageInfo), // ssr prefilter variance.
            RHIPushWriteDescriptorSetImage(35, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &ssrIntersectHistoryInfo), // in ssr prefilter radiance.
            RHIPushWriteDescriptorSetImage(36, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &ssrVarianceHistoryInfo), // in ssr prefilter variance.
            RHIPushWriteDescriptorSetImage(37, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &ssrIntersectImageInfo), // ssr temporal radiance.
            RHIPushWriteDescriptorSetImage(38, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &ssrVarianceImageInfo), // ssr temporal variance.
            RHIPushWriteDescriptorSetBuffer(39, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &frameBufferInfo), // framebuffer info.
            RHIPushWriteDescriptorSetImage(40, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &probe0Info),
            RHIPushWriteDescriptorSetImage(41, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &probe1Info),
        };

        if (m_history.sssrResources.sets.empty())
        {
            m_history.sssrResources.sets.resize(getContext()->getSwapchain().getBackbufferCount());
            for (size_t i = 0; i < getContext()->getSwapchain().getBackbufferCount(); i++)
            {
                m_history.sssrResources.sets[i] = pass->allocateSet();
            }
        }

        auto& setActive = m_history.sssrResources.sets[m_renderIndex % getContext()->getSwapchain().getBackbufferCount()];

        for (auto& write : writes)
        {
            write.dstSet = setActive;
        }
        vkUpdateDescriptorSets(getContext()->getDevice(), (uint32_t)writes.size(), writes.data(), 0, nullptr);

        std::vector<VkDescriptorSet> passSets =
        {
            setActive,
            getContext()->getSamplerCache().getCommonDescriptorSet(), // samplers.
            getRenderer()->getBlueNoise().spp_1_buffer.set
        };

        SSSRPush pushConst =
        {
            .samplesPerQuad = 1,
            .temporalVarianceGuidedTracingEnabled = 1,
            .mostDetailedMip = 0,
            .roughnessThreshold = 0.2f,
            .temporalVarianceThreshold = 0.0f,
        };

        pushConst.probe0ValidFactor = reflectionProbeContext.probe0ValidState;
        pushConst.probe1ValidFactor = reflectionProbeContext.probe1ValidState;
        pushConst.probe0Pos = reflectionProbeContext.probe0Position;
        pushConst.probe1Pos = reflectionProbeContext.probe1Position;
        pushConst.boxExtentData0 = vec4(reflectionProbeContext.probe0MinExtent, reflectionProbeContext.probe1MinExtent.x);
        pushConst.boxExtentData1 = vec4(reflectionProbeContext.probe0MaxExtent, reflectionProbeContext.probe1MinExtent.y);
        pushConst.boxExtentData2 = vec4(reflectionProbeContext.probe1MaxExtent, reflectionProbeContext.probe1MinExtent.z);

        pass->intersect->bindAndPushConst(cmd, &pushConst);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pass->intersect->pipelineLayout, 0, (uint32_t)passSets.size(), passSets.data(), 0, nullptr);
        {
            ScopePerframeMarker marker(cmd, "classify ssr", { 1.0f, 1.0f, 0.0f, 1.0f }, nullptr);

            m_history.sssrResources.rt_ssrRoughness->getImage().transitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL, buildBasicImageSubresource());
            m_history.sssrResources.rt_ssrRadiance->getImage().transitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL, buildBasicImageSubresource());
            pass->tileClassifyPipeline->bind(cmd);

            vkCmdDispatch(cmd, getGroupCount(m_history.sssrResources.rt_ssrRadiance->getImage().getExtent().width, 8), getGroupCount(m_history.sssrResources.rt_ssrRadiance->getImage().getExtent().height, 8), 1);

            m_history.sssrResources.rt_ssrRoughness->getImage().transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, buildBasicImageSubresource());
            m_history.sssrResources.rt_ssrRadiance->getImage().transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, buildBasicImageSubresource());

            // 
            std::array<VkBufferMemoryBarrier2, 3> endBufferBarriers
            {
                RHIBufferBarrier(ssboCounterBuffer->getBuffer()->getVkBuffer(),
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_MEMORY_WRITE_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  VK_ACCESS_SHADER_READ_BIT),

                RHIBufferBarrier(ssboRayListBuffer->getBuffer()->getVkBuffer(),
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_MEMORY_WRITE_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_MEMORY_READ_BIT),

                RHIBufferBarrier(ssboDenoiseListBuffer->getBuffer()->getVkBuffer(),
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_MEMORY_WRITE_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_MEMORY_READ_BIT),
            };
            RHIPipelineBarrier(cmd, 0, (uint32_t)endBufferBarriers.size(), endBufferBarriers.data(), 0, nullptr);
        }

        {
            ScopePerframeMarker marker(cmd, "ssr prepare args", { 1.0f, 1.0f, 0.0f, 1.0f }, nullptr);
            pass->argsPrepare->bind(cmd);
            vkCmdDispatch(cmd, 1, 1, 1);


            std::array<VkBufferMemoryBarrier2, 2> endBufferBarriers
            {
                RHIBufferBarrier(ssboIntersectCmdBuffer->getBuffer()->getVkBuffer(),
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_MEMORY_WRITE_BIT,
                    VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,  VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT),

                RHIBufferBarrier(ssboDenoiseCmdBuffer->getBuffer()->getVkBuffer(),
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_MEMORY_WRITE_BIT,
                    VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,  VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT),
            };
            RHIPipelineBarrier(cmd, 0, (uint32_t)endBufferBarriers.size(), endBufferBarriers.data(), 0, nullptr);
        }

        {
            ScopePerframeMarker marker(cmd, "ssr intersect", { 1.0f, 1.0f, 0.0f, 1.0f }, nullptr);

            m_history.sssrResources.rt_ssrRadiance->getImage().transitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL, buildBasicImageSubresource());
            pass->intersect->bind(cmd);

            vkCmdDispatchIndirect(cmd, ssboIntersectCmdBuffer->getBuffer()->getVkBuffer(), 0);

            m_history.sssrResources.rt_ssrRadiance->getImage().transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, buildBasicImageSubresource());
        }
        m_gpuTimer.getTimeStamp(cmd, "SSSR-Hit");
        {
            ScopePerframeMarker marker(cmd, "ssr reproject", { 1.0f, 1.0f, 0.0f, 1.0f }, nullptr);

            m_history.sssrResources.rt_ssrReproject->getImage().transitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL, buildBasicImageSubresource());
            m_history.sssrResources.rt_ssrAverageRadiance->getImage().transitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL, buildBasicImageSubresource());
            m_history.sssrResources.rt_ssrVariance->getImage().transitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL, buildBasicImageSubresource());
            m_history.sssrResources.rt_ssrSampleCount->getImage().transitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL, buildBasicImageSubresource());
            pass->reproject->bind(cmd);
            vkCmdDispatchIndirect(cmd, ssboDenoiseCmdBuffer->getBuffer()->getVkBuffer(), 0);


            m_history.sssrResources.rt_ssrReproject->getImage().transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, buildBasicImageSubresource());
            m_history.sssrResources.rt_ssrAverageRadiance->getImage().transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, buildBasicImageSubresource());
            m_history.sssrResources.rt_ssrVariance->getImage().transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, buildBasicImageSubresource());
            m_history.sssrResources.rt_ssrSampleCount->getImage().transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, buildBasicImageSubresource());
        }

        {
            ScopePerframeMarker marker(cmd, "ssr prefilter", { 1.0f, 1.0f, 0.0f, 1.0f }, nullptr);

            m_history.sssrResources.rt_ssrPrevRadiance->getImage().transitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL, buildBasicImageSubresource());
            m_history.sssrResources.rt_ssrPrevVariance->getImage().transitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL, buildBasicImageSubresource());
            pass->prefilter->bind(cmd);
            vkCmdDispatchIndirect(cmd, ssboDenoiseCmdBuffer->getBuffer()->getVkBuffer(), 0);

            m_history.sssrResources.rt_ssrPrevRadiance->getImage().transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, buildBasicImageSubresource());
            m_history.sssrResources.rt_ssrPrevVariance->getImage().transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, buildBasicImageSubresource());
        }

        {
            ScopePerframeMarker marker(cmd, "ssr temporal", { 1.0f, 1.0f, 0.0f, 1.0f }, nullptr);

            m_history.sssrResources.rt_ssrRadiance->getImage().transitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL, buildBasicImageSubresource());
            m_history.sssrResources.rt_ssrVariance->getImage().transitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL, buildBasicImageSubresource());
            pass->temporal->bind(cmd);
            vkCmdDispatchIndirect(cmd, ssboDenoiseCmdBuffer->getBuffer()->getVkBuffer(), 0);

            m_history.sssrResources.rt_ssrRadiance->getImage().transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, buildBasicImageSubresource());
            m_history.sssrResources.rt_ssrVariance->getImage().transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, buildBasicImageSubresource());
        }

        {
            ScopePerframeMarker marker(cmd, "ssr apply", { 1.0f, 1.0f, 0.0f, 1.0f }, nullptr);

            hdrSceneColor.transitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL, buildBasicImageSubresource());
            pass->apply->bind(cmd);
            vkCmdDispatch(cmd, getGroupCount(hdrSceneColor.getExtent().width, 8), getGroupCount(hdrSceneColor.getExtent().height, 8), 1);

            hdrSceneColor.transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, buildBasicImageSubresource());
        }

        {
            auto tempRadiance = m_history.sssrResources.rt_ssrPrevRadiance;
            auto tempVariance = m_history.sssrResources.rt_ssrPrevVariance;
            auto tempRoughness = m_history.sssrResources.rt_ssrPrevRoughness;
            auto tempSampleCount = m_history.sssrResources.rt_ssrPrevSampleCount;

            m_history.sssrResources.rt_ssrPrevRadiance = m_history.sssrResources.rt_ssrRadiance;
            m_history.sssrResources.rt_ssrPrevVariance = m_history.sssrResources.rt_ssrVariance;
            m_history.sssrResources.rt_ssrPrevRoughness = m_history.sssrResources.rt_ssrRoughness;
            m_history.sssrResources.rt_ssrPrevSampleCount = m_history.sssrResources.rt_ssrSampleCount;

            m_history.sssrResources.rt_ssrRadiance = tempRadiance;
            m_history.sssrResources.rt_ssrVariance = tempVariance;
            m_history.sssrResources.rt_ssrRoughness = tempRoughness;
            m_history.sssrResources.rt_ssrSampleCount = tempSampleCount;
        }
        m_gpuTimer.getTimeStamp(cmd, "SSSR-Filter");
    }
}
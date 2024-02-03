#include "../deferred_renderer.h"
#include "../render_scene.h"
#include "../renderer.h"
#include "../scene_textures.h"

namespace engine
{
    class GridPass : public PassInterface
    {
    public:
        std::unique_ptr<GraphicPipeResources> pipe;


    protected:
        virtual void onInit() override
        {
            VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;

            auto colorBlendAttachment = RHIColorBlendAttachmentOpauqeState();
            colorBlendAttachment.blendEnable = VK_TRUE;
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

            getContext()->descriptorFactoryBegin()
                .bindNoInfo(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0)
                .bindNoInfo(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  1)
                .buildNoInfoPush(setLayout);

            pipe = std::make_unique<GraphicPipeResources>(
                "shader/editor_grid.glsl",
                std::vector<VkDescriptorSetLayout>
                {
                    setLayout,
                    getContext()->getSamplerCache().getCommonDescriptorSetLayout(),
                },
                0,
                std::vector<VkFormat>
                {
                    getContext()->getSwapchain().getImageFormat(),
                },
                std::vector<VkPipelineColorBlendAttachmentState>
                {
                    colorBlendAttachment,
                },
                VK_FORMAT_UNDEFINED,
                VK_CULL_MODE_NONE,
                VK_COMPARE_OP_ALWAYS);
        }

        virtual void release() override
        {
            pipe.reset();
        }
    };

    void DeferredRenderer::renderGrid(
        VkCommandBuffer cmd,
        GBufferTextures* inGBuffers,
        BufferParameterHandle perFrameGPU)
    {
        auto& sceneColor = getOutput()->getImage();
        auto& sceneDepthZ = inGBuffers->depthTexture->getImage();

        sceneColor.transitionLayout(cmd, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, RHIDefaultImageSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT));
        sceneDepthZ.transitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, RHIDefaultImageSubresourceRange(VK_IMAGE_ASPECT_DEPTH_BIT));

        std::vector<VkRenderingAttachmentInfo> colorAttachments = ColorAttachmentsBuilder()
            .add(sceneColor, VK_ATTACHMENT_LOAD_OP_LOAD)
            .result;

        auto* pass = getContext()->getPasses().get<GridPass>();
        {
            ScopeRenderCmdObject renderCmdScope(cmd, &m_gpuTimer, "GridRendering", sceneColor, colorAttachments, {});

            pass->pipe->bind(cmd);
            PushSetBuilder(cmd)
                .addBuffer(perFrameGPU)
                .addSRV(sceneDepthZ, RHIDefaultImageSubresourceRange(VK_IMAGE_ASPECT_DEPTH_BIT))
                .push(pass->pipe.get());


            pass->pipe->bindSet(cmd,
                std::vector<VkDescriptorSet>{
                getContext()->getSamplerCache().getCommonDescriptorSet(),
            }, 1);

            vkCmdDraw(cmd, 6, 1, 0, 0);
        }

    }
}
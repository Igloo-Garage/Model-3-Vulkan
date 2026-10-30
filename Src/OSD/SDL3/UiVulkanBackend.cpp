/**
 ** Model 3-Vulkan
 ** A Vulkan-focused fork of Supermodel, the Sega Model 3 arcade emulator.
 ** Copyright (C) 2026 REXABYTE SOFTWARE INC. (Igloo-Garage / Rex)
 **
 ** This file is part of Model 3-Vulkan, and is derived from Supermodel,
 ** Copyright 2003-2026 The Supermodel Team.
 **
 ** Additional Terms under GNU GPLv3 Section 7 apply to this file
 ** (see LICENSE-ADDITIONAL-TERMS.md).
 **
 ** Supermodel is free software: you can redistribute it and/or modify it under
 ** the terms of the GNU General Public License as published by the Free
 ** Software Foundation, either version 3 of the License, or (at your option)
 ** any later version.
 **
 ** Supermodel is distributed in the hope that it will be useful, but WITHOUT
 ** ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 ** FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 ** more details.
 **
 ** You should have received a copy of the GNU General Public License along
 ** with Supermodel.  If not, see <http://www.gnu.org/licenses/>.
 **/

/*
 * UiVulkanBackend.cpp
 *
 * See UiVulkanBackend.h.  Only the SDL3 Pure-Vulkan target compiles this file.
 */

#include "OSD/SDL3/UiVulkanBackend.h"

#include "Graphics/Vulkan3D/UiBlur.h"
#include "Graphics/Vulkan3D/VulkanContext.h"
#include "Graphics/Vulkan3D/Shaders/composite_vert.spv.h"
#include "Graphics/Vulkan3D/Shaders/ui_ambient_frag.spv.h"
#include "OSD/Logger.h"
#include "OSD/SDL/Gui.h"          // UiFrost::AmbientColours(), UiFrost::Plate()

#include "imgui/imgui_impl_vulkan.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <utility>

namespace OSD
{
namespace SDL3App
{

namespace
{
// Mirrors the push constant block in ui_ambient.frag. Keep the two in step.
struct AmbientPush
{
    float uvScale[2];
    float uvOffset[2];
    float tint[4];      // rgb = wash colour, a = how far the cover is pulled toward it
    float params[4];    // x = saturation, y = vignette, z = exposure, w = unused
};

ImTextureID TextureHandle(VkDescriptorSet descriptor)
{
    return static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(descriptor));
}

VkDescriptorSet TextureDescriptor(ImTextureID texture)
{
    return reinterpret_cast<VkDescriptorSet>(static_cast<std::uintptr_t>(texture));
}
} // namespace

UiVulkanBackend::UiVulkanBackend() = default;

UiVulkanBackend::~UiVulkanBackend()
{
    Shutdown();
}

std::expected<void, std::string> UiVulkanBackend::Init(
    OSD::Platform::Window* window, Vulkan3D::CVulkanContext& context)
{
    if (m_ready)
        return std::unexpected(std::string("The Vulkan settings-window backend is already initialised."));

    m_context = &context;
    m_window = window;

    auto upload = CreateUploadObjects();
    if (!upload)
        return upload;

    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = 0.0f;
    if (vkCreateSampler(context.Device(), &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS)
        return std::unexpected(std::string("Could not create the sampler used by the boxart covers."));

    /*
     * One descriptor per boxart plus the font atlas.  The cache holds at most
     * one entry per ROM set (63 today), and the pool cannot grow after it is
     * created, so it is sized for the whole table rather than the visible few.
     */
    ImGui_ImplVulkan_InitInfo init = {};
    init.ApiVersion = VK_API_VERSION_1_3;
    init.Instance = context.Instance();
    init.PhysicalDevice = context.PhysicalDevice();
    init.Device = context.Device();
    init.QueueFamily = context.QueueFamily();
    init.Queue = context.Queue();
    // Shares the settings window's persistent pipeline cache (see CVulkanContext), so the ImGui
    // pipeline is not rebuilt from scratch on every start either.
    init.PipelineCache = context.PipelineCache();
    init.DescriptorPoolSize = 128;
    init.MinImageCount = 2;
    init.ImageCount = context.SwapImageCount() > 2 ? context.SwapImageCount() : 2;
    init.UseDynamicRendering = true;

    VkFormat format = context.SwapFormat();
    init.PipelineInfoMain.RenderPass = VK_NULL_HANDLE;
    init.PipelineInfoMain.Subpass = 0;
    init.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    init.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    init.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &format;

    if (!ImGui_ImplVulkan_Init(&init))
    {
        return std::unexpected(std::string(
            "The ImGui Vulkan backend could not be initialised for the settings window."));
    }

    m_generation = context.SwapchainGeneration();
    m_imageSeen.assign(context.SwapImageCount(), 0);

    /*
     * The frosted backdrop chain, created with the context rather than on first use: GUI() asks for
     * its texture before it builds the frame, and the texture cannot exist before the images do.
     */
    m_blur = std::make_unique<Vulkan3D::CUiBlur>(context);
    if (!m_blur->Ensure(context.Extent()))
    {
        InfoLog("GUI: frosted glass unavailable; panels will use a solid tint.");
        m_blur.reset();
    }
    if (!CreateAmbientPipeline())
        InfoLog("GUI: ambient backdrop unavailable; the window keeps a flat background.");

    m_ready = true;
    InfoLog("GUI: settings window on Vulkan (%ux%u, %u swapchain image(s)).",
        context.Extent().width, context.Extent().height, init.ImageCount);
    return {};
}

void UiVulkanBackend::Shutdown()
{
    if (m_context == nullptr)
        return;

    VkDevice device = m_context->Device();
    if (device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(device);

    // The ImGui backend owns the descriptor pool, so its descriptors have to
    // be returned before the pool and before the images are destroyed.
    if (m_ready)
        ImGui_ImplVulkan_Shutdown();
    m_ready = false;

    m_frostTexture = 0;
    m_frostView = VK_NULL_HANDLE;
    m_ambientTexture = 0;
    DestroyAmbient();
    if (m_blur != nullptr)
    {
        m_blur->Destroy();
        m_blur.reset();
    }

    for (UiTexture& texture : m_textures)
        m_context->Memory().DestroyImage(texture.image);
    m_textures.clear();

    if (device != VK_NULL_HANDLE)
    {
        if (m_sampler != VK_NULL_HANDLE)
            vkDestroySampler(device, m_sampler, nullptr);
        if (m_uploadFence != VK_NULL_HANDLE)
            vkDestroyFence(device, m_uploadFence, nullptr);
        if (m_uploadPool != VK_NULL_HANDLE)
            vkDestroyCommandPool(device, m_uploadPool, nullptr);
    }

    m_sampler = VK_NULL_HANDLE;
    m_uploadFence = VK_NULL_HANDLE;
    m_uploadPool = VK_NULL_HANDLE;
    m_uploadCmd = VK_NULL_HANDLE;
    m_imageSeen.clear();
    m_window = nullptr;
    m_context = nullptr;
}

std::expected<void, std::string> UiVulkanBackend::CreateUploadObjects()
{
    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = m_context->QueueFamily();
    if (vkCreateCommandPool(m_context->Device(), &poolInfo, nullptr, &m_uploadPool) != VK_SUCCESS)
        return std::unexpected(std::string("Could not create the texture upload command pool."));

    VkCommandBufferAllocateInfo alloc = {};
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool = m_uploadPool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(m_context->Device(), &alloc, &m_uploadCmd) != VK_SUCCESS)
        return std::unexpected(std::string("Could not allocate the texture upload command buffer."));

    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(m_context->Device(), &fenceInfo, nullptr, &m_uploadFence) != VK_SUCCESS)
        return std::unexpected(std::string("Could not create the texture upload fence."));

    return {};
}

std::expected<void, std::string> UiVulkanBackend::UploadPixels(
    const std::uint8_t* rgba, int width, int height, Vulkan3D::AllocatedImage& image)
{
    const VkDeviceSize bytes = VkDeviceSize(width) * VkDeviceSize(height) * 4;

    Vulkan3D::AllocatedBuffer staging;
    if (!m_context->Memory().CreateBuffer(bytes,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            staging))
    {
        return std::unexpected(std::format(
            "Could not allocate {} bytes of staging memory for a {}x{} cover.", bytes, width, height));
    }

    std::memcpy(staging.mapped, rgba, static_cast<std::size_t>(bytes));

    if (!m_context->Memory().CreateImage(static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1,
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            image))
    {
        m_context->Memory().DestroyBuffer(staging);
        return std::unexpected(std::format("Could not create the {}x{} cover image.", width, height));
    }

    image.view = m_context->CreateView(image.image, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_ASPECT_COLOR_BIT, 0, 1);
    if (image.view == VK_NULL_HANDLE)
    {
        m_context->Memory().DestroyImage(image);
        m_context->Memory().DestroyBuffer(staging);
        return std::unexpected(std::format("Could not create a view for the {}x{} cover image.", width, height));
    }

    vkResetCommandPool(m_context->Device(), m_uploadPool, 0);
    VkCommandBufferBeginInfo begin = {};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(m_uploadCmd, &begin);

    Vulkan3D::CmdTransition(m_uploadCmd, image.image,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT);

    VkBufferImageCopy region = {};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
    vkCmdCopyBufferToImage(m_uploadCmd, staging.buffer, image.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    Vulkan3D::CmdTransition(m_uploadCmd, image.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT);

    vkEndCommandBuffer(m_uploadCmd);

    VkCommandBufferSubmitInfo cmdInfo = {};
    cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdInfo.commandBuffer = m_uploadCmd;

    VkSubmitInfo2 submit = {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmdInfo;

    Vulkan3D::QueueSubmit2(m_context->Queue(), 1, &submit, m_uploadFence);
    vkWaitForFences(m_context->Device(), 1, &m_uploadFence, VK_TRUE, UINT64_MAX);
    vkResetFences(m_context->Device(), 1, &m_uploadFence);

    m_context->Memory().DestroyBuffer(staging);
    return {};
}

bool UiVulkanBackend::BeginFrame()
{
    if (!m_ready || m_context == nullptr)
        return false;
    if (!m_context->BeginFrame())
        return false;

    /*
     * The chain is sized to the swapchain here rather than in Render(), because GUI() asks
     * FrostTexture() for a handle between this call and Render() - a resize that was only noticed
     * at submit time would leave the frame it was noticed on with a handle to a destroyed image.
     */
    if (m_blur != nullptr && !m_blur->Ensure(m_context->Extent()))
    {
        m_blur->Destroy();
        m_blur.reset();
        m_frostTexture = 0;
        m_frostView = VK_NULL_HANDLE;
    }
    return true;
}

void UiVulkanBackend::Render(ImDrawData* drawData)
{
    if (!m_ready || m_context == nullptr || drawData == nullptr || !m_context->IsRecording())
        return;

    const uint64_t generation = m_context->SwapchainGeneration();
    if (generation != m_generation)
    {
        m_generation = generation;
        m_imageSeen.assign(m_context->SwapImageCount(), 0);
    }

    const uint32_t imageIndex = m_context->ImageIndex();
    const bool seen = imageIndex < m_imageSeen.size() && m_imageSeen[imageIndex] != 0;
    const VkImageLayout oldLayout = seen ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED;

    VkCommandBuffer cmd = m_context->Cmd();
    const VkExtent2D extent = m_context->Extent();

    Vulkan3D::CmdTransition(cmd, m_context->SwapImage(),
        oldLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);

    /*
     * Ambient backdrop first, into the swapchain.
     *
     * The window used to be cleared to a flat colour and the interface drawn straight onto it. With
     * frosted panels that leaves nothing for the glass to show, so the selected game's cover is
     * painted across the whole window instead, and the blur chain reads the result. It has to be a
     * real pass rather than an ImGui background image, because the image would be part of the same
     * draw data the panels are in - there would be no point at which the frame held the backdrop and
     * not yet the interface.
     */
    const UiFrost::AmbientWash wash = UiFrost::AmbientColours();
    const bool haveBackdrop = ResolveAmbientView();

    VkRenderingAttachmentInfo color = {};
    color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color.imageView = m_context->SwapView();
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    // The wash colour doubles as the fallback clear, so a window with no cover selected and a window
    // with one look like the same program.
    color.clearValue.color = { { wash.tint.x, wash.tint.y, wash.tint.z, 1.0f } };

    VkRenderingInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.renderArea = { { 0, 0 }, extent };
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &color;

    Vulkan3D::CmdBeginRendering(cmd, &info);
    if (haveBackdrop && m_ambientPipeline != VK_NULL_HANDLE)
        DrawAmbientPass(cmd, extent);
    Vulkan3D::CmdEndRendering(cmd);

    /*
     * Blur the backdrop, but only when it can have changed.
     *
     * The settings window is a still picture: the cover changes when the selection does, and the
     * chain is rebuilt when the window is resized. Everything else is identical frame to frame, so
     * the blurred copy from the previous frame is still exactly right - this is the "lazy update"
     * the effect depends on to be free while the launcher is sitting idle.
     */
    if (m_blur != nullptr && m_blur->Ready())
    {
        std::uint64_t key = reinterpret_cast<std::uintptr_t>(m_ambientView);
        key = key * 0x9E3779B97F4A7C15ull ^ ((std::uint64_t)extent.width << 32 | extent.height);
        key ^= (std::uint64_t)m_blur->Generation() * 0x94D049BB133111EBull;
        if (!UiThemeIsDark())
            key ^= 0x5D2B3C4F71A9ull;
        if (key != m_backdropKey || !m_backdropBlurred)
        {
            m_blur->Record(cmd, m_context->SwapImage());
            m_backdropKey = key;
            m_backdropBlurred = true;
        }
    }

    // Load rather than clear: the backdrop is already there and the interface composites over it.
    color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    Vulkan3D::CmdBeginRendering(cmd, &info);
    ImGui_ImplVulkan_RenderDrawData(drawData, cmd);
    Vulkan3D::CmdEndRendering(cmd);

    Vulkan3D::CmdTransition(cmd, m_context->SwapImage(),
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_IMAGE_ASPECT_COLOR_BIT);

    if (imageIndex < m_imageSeen.size())
        m_imageSeen[imageIndex] = 1;

    m_context->MarkSwapchainReady();
    m_context->EndFrame();
}

std::expected<ImTextureID, std::string> UiVulkanBackend::AddTexture(
    const std::uint8_t* rgba, int width, int height)
{
    if (!m_ready || m_context == nullptr)
        return std::unexpected(std::string("The Vulkan settings-window backend is not initialised."));
    if (rgba == nullptr || width <= 0 || height <= 0)
        return std::unexpected(std::string("The cover image is empty."));

    UiTexture texture;
    auto uploaded = UploadPixels(rgba, width, height, texture.image);
    if (!uploaded)
        return std::unexpected(uploaded.error());

    texture.descriptor = ImGui_ImplVulkan_AddTexture(
        m_sampler, texture.image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (texture.descriptor == VK_NULL_HANDLE)
    {
        m_context->Memory().DestroyImage(texture.image);
        return std::unexpected(std::string(
            "The ImGui descriptor pool is full; the cover cannot be shown."));
    }

    m_textures.emplace_back(std::move(texture));
    return static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(m_textures.back().descriptor));
}

void UiVulkanBackend::RemoveTexture(ImTextureID texture)
{
    if (texture == 0 || m_context == nullptr)
        return;

    const VkDescriptorSet descriptor = reinterpret_cast<VkDescriptorSet>(
        static_cast<std::uintptr_t>(texture));

    const auto found = std::find_if(m_textures.begin(), m_textures.end(),
        [descriptor](const UiTexture& item) { return item.descriptor == descriptor; });
    if (found == m_textures.end())
        return;

    if (m_ready)
        ImGui_ImplVulkan_RemoveTexture(descriptor);
    m_context->Memory().DestroyImage(found->image);
    m_textures.erase(found);

    // The ambient descriptor names this image. Dropping the view makes the next frame either
    // re-resolve to a different cover or fall back to the flat clear, and either way the descriptor
    // is rewritten before it is used again.
    if (m_ambientTexture == texture)
    {
        m_ambientTexture = 0;
        m_ambientView = VK_NULL_HANDLE;
        m_backdropKey = 0;
    }
}

VkImageView UiVulkanBackend::ImageViewFor(ImTextureID texture) const
{
    if (texture == 0)
        return VK_NULL_HANDLE;

    const VkDescriptorSet descriptor = TextureDescriptor(texture);
    const auto found = std::find_if(m_textures.begin(), m_textures.end(),
        [descriptor](const UiTexture& item) { return item.descriptor == descriptor; });
    return found != m_textures.end() ? found->image.view : VK_NULL_HANDLE;
}

void UiVulkanBackend::SetAmbientTexture(ImTextureID texture)
{
    if (m_ambientTexture == texture)
        return;
    m_ambientTexture = texture;
    // Resolved on the next frame, when the handle can be looked up against the texture list.
    m_ambientView = VK_NULL_HANDLE;
    m_backdropKey = 0;
}

ImTextureID UiVulkanBackend::FrostTexture()
{
    if (!m_ready || m_context == nullptr || m_blur == nullptr || !m_blur->Ready())
        return 0;

    const VkImageView view = m_blur->View();
    if (view == VK_NULL_HANDLE)
        return 0;

    /*
     * A rebuilt chain means the registered descriptor names a destroyed image, so it goes with it.
     * The generation is compared rather than the view handle because the allocator reuses addresses:
     * a resize can hand back the same VkImageView while it names a different allocation.
     */
    if (m_frostGeneration != m_blur->Generation())
    {
        if (m_frostTexture != 0)
            ImGui_ImplVulkan_RemoveTexture(TextureDescriptor(m_frostTexture));
        m_frostTexture = 0;
        m_frostView = view;
        m_frostGeneration = m_blur->Generation();
        m_backdropBlurred = false;
    }

    /*
     * Nothing has been blurred into the image yet.
     *
     * Render() records the chain after the panel has been built, so on the frame a chain is created
     * the image is still undefined. Handing out its handle anyway would have ImGui sample
     * uninitialised memory for one frame; returning 0 makes the panel fall back to a solid tint,
     * which is invisible next to a single frame of noise.
     */
    if (!m_backdropBlurred)
        return 0;

    if (m_frostTexture == 0)
    {
        const VkDescriptorSet descriptor = ImGui_ImplVulkan_AddTexture(
            m_blur->Sampler(), view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (descriptor == VK_NULL_HANDLE)
            return 0;
        m_frostTexture = TextureHandle(descriptor);
    }
    return m_frostTexture;
}

bool UiVulkanBackend::CreateAmbientPipeline()
{
    VkDevice device = m_context->Device();

    VkShaderModuleCreateInfo moduleInfo = {};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = composite_vert_spv_size;
    moduleInfo.pCode = composite_vert_spv;
    if (vkCreateShaderModule(device, &moduleInfo, nullptr, &m_ambientVert) != VK_SUCCESS)
        return false;
    moduleInfo.codeSize = ui_ambient_frag_spv_size;
    moduleInfo.pCode = ui_ambient_frag_spv;
    if (vkCreateShaderModule(device, &moduleInfo, nullptr, &m_ambientFrag) != VK_SUCCESS)
        return false;

    VkDescriptorSetLayoutBinding bindings[2] = {};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_ambientLayout) != VK_SUCCESS)
        return false;

    VkDescriptorPoolSize sizes[2] = {};
    sizes[0].type = VK_DESCRIPTOR_TYPE_SAMPLER;
    sizes[0].descriptorCount = 1;
    sizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    sizes[1].descriptorCount = 1;
    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_ambientPool) != VK_SUCCESS)
        return false;

    VkDescriptorSetAllocateInfo alloc = {};
    alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool = m_ambientPool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &m_ambientLayout;
    if (vkAllocateDescriptorSets(device, &alloc, &m_ambientSet) != VK_SUCCESS)
        return false;

    VkDescriptorImageInfo samplerInfo = {};
    samplerInfo.sampler = m_sampler;
    VkWriteDescriptorSet samplerWrite = {};
    samplerWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    samplerWrite.dstSet = m_ambientSet;
    samplerWrite.dstBinding = 0;
    samplerWrite.descriptorCount = 1;
    samplerWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    samplerWrite.pImageInfo = &samplerInfo;
    vkUpdateDescriptorSets(device, 1, &samplerWrite, 0, nullptr);

    VkPushConstantRange pushRange = {};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(AmbientPush);
    VkPipelineLayoutCreateInfo pipeLayoutInfo = {};
    pipeLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeLayoutInfo.setLayoutCount = 1;
    pipeLayoutInfo.pSetLayouts = &m_ambientLayout;
    pipeLayoutInfo.pushConstantRangeCount = 1;
    pipeLayoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(device, &pipeLayoutInfo, nullptr, &m_ambientPipeLayout) != VK_SUCCESS)
        return false;

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = m_ambientVert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = m_ambientFrag;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput = {};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

    VkPipelineViewportStateCreateInfo viewport = {};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster = {};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multi = {};
    multi.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multi.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blend = {};
    blend.blendEnable = VK_FALSE;
    blend.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo blendState = {};
    blendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blendState.attachmentCount = 1;
    blendState.pAttachments = &blend;

    VkDynamicState dynamics[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic = {};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamics;

    VkFormat format = m_context->SwapFormat();
    VkPipelineRenderingCreateInfo rendering = {};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &format;

    VkGraphicsPipelineCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.pNext = &rendering;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &inputAssembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multi;
    info.pColorBlendState = &blendState;
    info.pDynamicState = &dynamic;
    info.layout = m_ambientPipeLayout;

    if (vkCreateGraphicsPipelines(device, m_context->PipelineCache(), 1, &info, nullptr, &m_ambientPipeline) != VK_SUCCESS)
    {
        ErrorLog("GUI: the ambient backdrop pipeline failed to build.");
        return false;
    }
    return true;
}

bool UiVulkanBackend::ResolveAmbientView()
{
    const VkImageView view = ImageViewFor(m_ambientTexture);
    if (view == m_ambientView && view != VK_NULL_HANDLE)
        return true;

    m_ambientView = view;
    m_backdropKey = 0;
    if (view == VK_NULL_HANDLE)
        return false;

    VkDescriptorImageInfo image = {};
    image.imageView = view;
    image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_ambientSet;
    write.dstBinding = 1;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    write.pImageInfo = &image;
    vkUpdateDescriptorSets(m_context->Device(), 1, &write, 0, nullptr);
    return true;
}

void UiVulkanBackend::DrawAmbientPass(VkCommandBuffer cmd, VkExtent2D extent)
{
    if (m_ambientView == VK_NULL_HANDLE || m_sampler == VK_NULL_HANDLE)
        return;

    int width = 0;
    int height = 0;
    for (const UiTexture& texture : m_textures)
    {
        if (texture.image.view == m_ambientView)
        {
            width = (int)texture.image.width;
            height = (int)texture.image.height;
            break;
        }
    }
    if (width <= 0 || height <= 0 || extent.width == 0 || extent.height == 0)
        return;

    const UiFrost::AmbientWash wash = UiFrost::AmbientColours();

    /*
     * Cover fit: the cover fills the window and is cropped, rather than being letterboxed.
     *
     * A portrait boxart in a landscape window would otherwise leave two thirds of the background
     * empty, and this is an abstract wash - the artwork is never meant to be legible, only to give
     * the glass something with texture to blur.
     */
    const float imageAspect = (float)width / (float)height;
    const float screenAspect = (float)extent.width / (float)extent.height;
    AmbientPush push = {};
    if (screenAspect > imageAspect)
    {
        const float scale = imageAspect / screenAspect;
        push.uvScale[0] = 1.0f;
        push.uvScale[1] = scale;
        push.uvOffset[0] = 0.0f;
        push.uvOffset[1] = (1.0f - scale) * 0.5f;
    }
    else
    {
        const float scale = screenAspect / imageAspect;
        push.uvScale[0] = scale;
        push.uvScale[1] = 1.0f;
        push.uvOffset[0] = (1.0f - scale) * 0.5f;
        push.uvOffset[1] = 0.0f;
    }
    push.tint[0] = wash.tint.x;
    push.tint[1] = wash.tint.y;
    push.tint[2] = wash.tint.z;
    push.tint[3] = wash.tint.w;
    push.params[0] = wash.saturation;
    push.params[1] = wash.vignette;
    push.params[2] = wash.exposure;
    push.params[3] = 0.0f;

    VkViewport viewport = {};
    viewport.width = (float)extent.width;
    viewport.height = (float)extent.height;
    viewport.maxDepth = 1.0f;
    VkRect2D scissor = { { 0, 0 }, extent };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ambientPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ambientPipeLayout, 0, 1, &m_ambientSet, 0, nullptr);
    vkCmdPushConstants(cmd, m_ambientPipeLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vkCmdDraw(cmd, 4, 1, 0, 0);
}

void UiVulkanBackend::DestroyAmbient()
{
    if (m_context == nullptr)
        return;
    VkDevice device = m_context->Device();
    if (device == VK_NULL_HANDLE)
        return;

    if (m_ambientPipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(device, m_ambientPipeline, nullptr);
    if (m_ambientPipeLayout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(device, m_ambientPipeLayout, nullptr);
    if (m_ambientPool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(device, m_ambientPool, nullptr);
    if (m_ambientLayout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(device, m_ambientLayout, nullptr);
    if (m_ambientVert != VK_NULL_HANDLE)
        vkDestroyShaderModule(device, m_ambientVert, nullptr);
    if (m_ambientFrag != VK_NULL_HANDLE)
        vkDestroyShaderModule(device, m_ambientFrag, nullptr);

    m_ambientPipeline = VK_NULL_HANDLE;
    m_ambientPipeLayout = VK_NULL_HANDLE;
    m_ambientPool = VK_NULL_HANDLE;
    m_ambientSet = VK_NULL_HANDLE;
    m_ambientLayout = VK_NULL_HANDLE;
    m_ambientVert = VK_NULL_HANDLE;
    m_ambientFrag = VK_NULL_HANDLE;
    m_ambientView = VK_NULL_HANDLE;
    m_backdropKey = 0;
    m_backdropBlurred = false;
}

} // SDL3App
} // OSD

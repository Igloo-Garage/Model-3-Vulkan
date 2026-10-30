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
 * UiVulkanBackend.h
 *
 * Hosts one ImGui context on a Vulkan swapchain without any OpenGL.
 *
 * The settings window used to own a GL context and draw ImGui through
 * imgui_impl_opengl3.  In the Pure-Vulkan executable the same window owns a
 * CVulkanContext instead and this class provides the three things the GL path
 * used to supply:
 *
 *   - one frame of ImGui draw data recorded into the swapchain image
 *     (dynamic rendering, because the backend is built for 1.3),
 *   - an upload path for the boxart textures, exposed as ImTextureID,
 *   - the matching teardown order.
 *
 * The class is used only by the SDL3 Pure-Vulkan target.  The SDL2 build keeps
 * imgui_impl_opengl3 and never compiles this file.
 */

#ifndef INCLUDED_OSD_SDL3_UIVULKANBACKEND_H
#define INCLUDED_OSD_SDL3_UIVULKANBACKEND_H

#include "Graphics/Vulkan3D/VulkanCommon.h"
#include "Graphics/Vulkan3D/VulkanMemory.h"

#include "imgui/imgui.h"

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <vector>

namespace OSD
{
namespace Platform
{
struct Window;
}
}

namespace Vulkan3D
{
class CVulkanContext;
class CUiBlur;
}

namespace OSD
{
namespace SDL3App
{

class UiVulkanBackend
{
public:
    UiVulkanBackend();
    ~UiVulkanBackend();

    UiVulkanBackend(const UiVulkanBackend&) = delete;
    UiVulkanBackend& operator=(const UiVulkanBackend&) = delete;

    /*
     * The ImGui context must already exist and be current.  On failure the
     * returned error is a complete English sentence for the log.
     */
    std::expected<void, std::string> Init(
        OSD::Platform::Window* window, Vulkan3D::CVulkanContext& context);

    void Shutdown();

    /*
     * Acquires a swapchain image and starts the frame command buffer.  Returns
     * false when the window is minimised or the swapchain is being recreated;
     * the caller must then skip the ImGui frame.
     */
    bool BeginFrame();

    /* Records and presents the finished ImGui draw data. */
    void Render(ImDrawData* drawData);

    /*
     * Uploads 8-bit RGBA pixels and registers them with the ImGui Vulkan
     * backend.  The returned handle stays valid until RemoveTexture().
     */
    std::expected<ImTextureID, std::string> AddTexture(
        const std::uint8_t* rgba, int width, int height);

    void RemoveTexture(ImTextureID texture);

    bool Ready() const { return m_ready; }

    /*
     * Cover art used as the full-window ambient backdrop, behind the frosted panels.
     *
     * Pass a handle returned by AddTexture(), or 0 to fall back to a flat fill. The backdrop is what
     * the frosted panels have to blur - over a plain window clear there is nothing for the material
     * to show - so this is set from the selection in the game list.
     */
    void SetAmbientTexture(ImTextureID texture);

    /*
     * The blurred copy of the backdrop, as a texture handle the interface can sample, or 0 while
     * nothing has been blurred yet.
     *
     * Asked for once per frame, after BeginFrame(). Re-registers itself when a resize has replaced
     * the image; the caller only has to notice that the returned handle changed.
     */
    ImTextureID FrostTexture();

private:
    struct UiTexture
    {
        Vulkan3D::AllocatedImage image;
        VkDescriptorSet descriptor = VK_NULL_HANDLE;
    };

    std::expected<void, std::string> CreateUploadObjects();
    std::expected<void, std::string> UploadPixels(
        const std::uint8_t* rgba, int width, int height, Vulkan3D::AllocatedImage& image);

    // Ambient-backdrop pass: one textured full-screen quad, drawn straight into the swapchain
    // before the interface so the frosted panels have something to sample.
    bool CreateAmbientPipeline();
    bool ResolveAmbientView();
    void UpdateAmbientDescriptor();
    void DrawAmbientPass(VkCommandBuffer cmd, VkExtent2D extent);
    void DestroyAmbient();

    VkImageView ImageViewFor(ImTextureID texture) const;

    Vulkan3D::CVulkanContext* m_context = nullptr;
    OSD::Platform::Window* m_window = nullptr;

    VkSampler m_sampler = VK_NULL_HANDLE;
    VkCommandPool m_uploadPool = VK_NULL_HANDLE;
    VkCommandBuffer m_uploadCmd = VK_NULL_HANDLE;
    VkFence m_uploadFence = VK_NULL_HANDLE;

    // Which swapchain images this backend has already presented, so the first
    // transition can come from UNDEFINED and later ones from PRESENT_SRC.
    std::vector<std::uint8_t> m_imageSeen;
    std::uint64_t m_generation = 0;

    std::vector<UiTexture> m_textures;

    // Ambient backdrop and its blur. The chain is rebuilt with the swapchain; the blur itself is
    // only re-recorded when the cover or the theme changes, which is what makes a stationary
    // settings window cost nothing after its first frame.
    std::unique_ptr<Vulkan3D::CUiBlur> m_blur;
    ImTextureID m_ambientTexture = 0;
    ImTextureID m_frostTexture = 0;
    VkImageView m_ambientView = VK_NULL_HANDLE;
    VkImageView m_frostView = VK_NULL_HANDLE;
    VkSampler m_ambientSampler = VK_NULL_HANDLE;
    VkShaderModule m_ambientVert = VK_NULL_HANDLE;
    VkShaderModule m_ambientFrag = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_ambientLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_ambientPool = VK_NULL_HANDLE;
    VkDescriptorSet m_ambientSet = VK_NULL_HANDLE;
    VkPipelineLayout m_ambientPipeLayout = VK_NULL_HANDLE;
    VkPipeline m_ambientPipeline = VK_NULL_HANDLE;
    std::uint64_t m_backdropKey = 0;
    std::uint32_t m_frostGeneration = 0;
    bool m_backdropBlurred = false;
    bool m_ready = false;
};

} // SDL3App
} // OSD

#endif // INCLUDED_OSD_SDL3_UIVULKANBACKEND_H

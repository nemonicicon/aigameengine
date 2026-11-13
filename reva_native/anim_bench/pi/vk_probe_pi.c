/* vk_probe_pi.c -- what Vulkan on the Pi can see of the display: VK_KHR_display's displays, modes and planes, memory types,
 * and the extensions that would let the engine keep its own KMS flip loop. Reads only: no modeset, no effect on the TV.
 * Build: gcc -O2 -o vk_probe_pi vk_probe_pi.c -lvulkan */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

int main(void){
    const char*ie[]={VK_KHR_SURFACE_EXTENSION_NAME,"VK_KHR_display","VK_EXT_direct_mode_display","VK_EXT_acquire_drm_display","VK_KHR_get_physical_device_properties2","VK_KHR_get_surface_capabilities2","VK_KHR_get_display_properties2"};
    VkApplicationInfo ai={VK_STRUCTURE_TYPE_APPLICATION_INFO}; ai.apiVersion=VK_API_VERSION_1_3;
    VkInstanceCreateInfo ci={VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ci.pApplicationInfo=&ai; ci.enabledExtensionCount=7; ci.ppEnabledExtensionNames=ie;
    VkInstance inst; VkResult r=vkCreateInstance(&ci,NULL,&inst); if(r){ printf("instance failed %d\n",r); return 1; }
    uint32_t n=0; vkEnumeratePhysicalDevices(inst,&n,NULL); VkPhysicalDevice pd[8]; if(n>8)n=8; vkEnumeratePhysicalDevices(inst,&n,pd);
    VkPhysicalDevice gpu=0;
    for(uint32_t i=0;i<n;i++){ VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(pd[i],&p); if(strstr(p.deviceName,"V3D")){ gpu=pd[i];
        printf("%s, Vulkan %u.%u.%u\n",p.deviceName,VK_VERSION_MAJOR(p.apiVersion),VK_VERSION_MINOR(p.apiVersion),VK_VERSION_PATCH(p.apiVersion));
        printf("limits: image %u, colour attachments %u, sampled images per stage %u, uniform buffers per stage %u, push constants %u, uniform buffer range %u, draw indirect count %u, timestamp period %.1f ns, timestamps all queues %u\n",
            p.limits.maxImageDimension2D,p.limits.maxColorAttachments,p.limits.maxPerStageDescriptorSampledImages,p.limits.maxPerStageDescriptorUniformBuffers,p.limits.maxPushConstantsSize,p.limits.maxUniformBufferRange,p.limits.maxDrawIndirectCount,p.limits.timestampPeriod,p.limits.timestampComputeAndGraphics); } }
    if(!gpu){ printf("no V3D device\n"); return 1; }

    VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(gpu,&mp);
    for(uint32_t i=0;i<mp.memoryHeapCount;i++) printf("memory heap %u: %.0f MB\n",i,mp.memoryHeaps[i].size/1048576.0);
    for(uint32_t i=0;i<mp.memoryTypeCount;i++){ VkFlags f=mp.memoryTypes[i].propertyFlags; printf("memory type %u:%s%s%s%s%s\n",i,(f&1)?" device-local":"",(f&2)?" host-visible":"",(f&4)?" coherent":"",(f&8)?" cached":"",(f&16)?" lazily-allocated":""); }

    uint32_t ne=0; vkEnumerateDeviceExtensionProperties(gpu,NULL,&ne,NULL); VkExtensionProperties*e=malloc(sizeof(*e)*ne); vkEnumerateDeviceExtensionProperties(gpu,NULL,&ne,e);
    const char*want[]={"VK_KHR_swapchain","VK_KHR_present_id","VK_KHR_present_wait","VK_EXT_display_control","VK_EXT_external_memory_dma_buf","VK_KHR_external_memory_fd","VK_EXT_image_drm_format_modifier","VK_EXT_physical_device_drm","VK_KHR_dynamic_rendering","VK_KHR_dynamic_rendering_local_read","VK_EXT_descriptor_indexing","VK_KHR_draw_indirect_count","VK_EXT_memory_budget","VK_KHR_incremental_present","VK_EXT_pipeline_creation_cache_control","VK_KHR_pipeline_executable_properties","VK_EXT_tooling_info","VK_KHR_maintenance4",NULL};
    printf("device extensions: %u total\n",ne);
    for(int w=0;want[w];w++){ int has=0; for(uint32_t i=0;i<ne;i++) if(!strcmp(e[i].extensionName,want[w])) has=1; printf("  %-42s %s\n",want[w],has?"yes":"no"); }

    VkPhysicalDeviceVulkan13Features f13={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceVulkan12Features f12={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES}; f12.pNext=&f13;
    VkPhysicalDeviceVulkan11Features f11={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES}; f11.pNext=&f12;
    VkPhysicalDeviceFeatures2 f2={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2}; f2.pNext=&f11; vkGetPhysicalDeviceFeatures2(gpu,&f2);
    printf("features: dynamicRendering %u, synchronization2 %u, timelineSemaphore %u, bufferDeviceAddress %u, descriptorIndexing %u, drawIndirectCount %u, multiDrawIndirect %u, samplerAnisotropy %u, textureCompressionBC %u, textureCompressionETC2 %u, textureCompressionASTC %u, geometryShader %u, tessellationShader %u, shaderDrawParameters %u, independentBlend %u, depthClamp %u, fillModeNonSolid %u, wideLines %u, shaderInt16 %u\n",
        f13.dynamicRendering,f13.synchronization2,f12.timelineSemaphore,f12.bufferDeviceAddress,f12.descriptorIndexing,f12.drawIndirectCount,f2.features.multiDrawIndirect,f2.features.samplerAnisotropy,f2.features.textureCompressionBC,f2.features.textureCompressionETC2,f2.features.textureCompressionASTC_LDR,f2.features.geometryShader,f2.features.tessellationShader,f11.shaderDrawParameters,f2.features.independentBlend,f2.features.depthClamp,f2.features.fillModeNonSolid,f2.features.wideLines,f2.features.shaderInt16);

    PFN_vkGetPhysicalDeviceDisplayPropertiesKHR gdp=(void*)vkGetInstanceProcAddr(inst,"vkGetPhysicalDeviceDisplayPropertiesKHR");
    PFN_vkGetDisplayModePropertiesKHR gmp=(void*)vkGetInstanceProcAddr(inst,"vkGetDisplayModePropertiesKHR");
    PFN_vkGetPhysicalDeviceDisplayPlanePropertiesKHR gpp=(void*)vkGetInstanceProcAddr(inst,"vkGetPhysicalDeviceDisplayPlanePropertiesKHR");
    uint32_t nd=0; r=gdp(gpu,&nd,NULL); printf("displays through VK_KHR_display: %u (result %d)\n",nd,r);
    if(nd){ VkDisplayPropertiesKHR*dp=malloc(sizeof(*dp)*nd); gdp(gpu,&nd,dp);
        for(uint32_t i=0;i<nd;i++){ printf(" display %u: %s, %ux%u, %ux%u mm\n",i,dp[i].displayName?dp[i].displayName:"(no name)",dp[i].physicalResolution.width,dp[i].physicalResolution.height,dp[i].physicalDimensions.width,dp[i].physicalDimensions.height);
            uint32_t nm=0; gmp(gpu,dp[i].display,&nm,NULL); VkDisplayModePropertiesKHR*mm=malloc(sizeof(*mm)*(nm?nm:1)); gmp(gpu,dp[i].display,&nm,mm);
            printf(" %u modes:\n",nm); for(uint32_t k=0;k<nm;k++) printf("   %ux%u at %.3f Hz\n",mm[k].parameters.visibleRegion.width,mm[k].parameters.visibleRegion.height,mm[k].parameters.refreshRate/1000.0); } }
    uint32_t np=0; r=gpp(gpu,&np,NULL); printf("display planes: %u (result %d)\n",np,r);
    vkDestroyInstance(inst,NULL);
    return 0;
}

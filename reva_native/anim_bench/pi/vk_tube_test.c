/* vk_tube_test.c -- Vulkan's own road to the tube (VK_KHR_display + a FIFO swapchain): how many new images a second it really
 * presents at 480i and 240p, in-spec NTSC modes only, dim colours, five seconds each. Also which compressed formats Vulkan samples.
 * Build: gcc -O2 -o vk_tube_test vk_tube_test.c -lvulkan     Run as root, console stopped. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vulkan/vulkan.h>

#define CK(x) do{ VkResult r_=(x); if(r_!=VK_SUCCESS&&r_!=VK_SUBOPTIMAL_KHR){ printf("FAILED %s: %d\n",#x,r_); exit(1); } }while(0)
static double nowMs(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1000.0+t.tv_nsec/1e6; }

static void fmt(VkPhysicalDevice g,VkFormat f,const char*n){ VkFormatProperties p; vkGetPhysicalDeviceFormatProperties(g,f,&p);
    printf("  %-22s sampled %s, linear filter %s\n",n,(p.optimalTilingFeatures&VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)?"yes":"no",(p.optimalTilingFeatures&VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)?"yes":"no"); }

int main(void){
    const char*ie[]={VK_KHR_SURFACE_EXTENSION_NAME,"VK_KHR_display"};
    VkApplicationInfo ai={VK_STRUCTURE_TYPE_APPLICATION_INFO}; ai.apiVersion=VK_API_VERSION_1_3;
    VkInstanceCreateInfo ci={VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ci.pApplicationInfo=&ai; ci.enabledExtensionCount=2; ci.ppEnabledExtensionNames=ie;
    VkInstance inst; CK(vkCreateInstance(&ci,NULL,&inst));
    uint32_t n=8; VkPhysicalDevice pd[8]; vkEnumeratePhysicalDevices(inst,&n,pd); VkPhysicalDevice g=0;
    for(uint32_t i=0;i<n;i++){ VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(pd[i],&p); if(strstr(p.deviceName,"V3D")) g=pd[i]; }
    if(!g){ printf("no V3D\n"); return 1; }
    printf("compressed formats through Vulkan:\n");
    fmt(g,VK_FORMAT_BC1_RGB_UNORM_BLOCK,"BC1"); fmt(g,VK_FORMAT_BC3_UNORM_BLOCK,"BC3"); fmt(g,VK_FORMAT_BC7_UNORM_BLOCK,"BC7");
    fmt(g,VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK,"ETC2 RGB"); fmt(g,VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK,"ETC2 RGBA"); fmt(g,VK_FORMAT_ASTC_4x4_UNORM_BLOCK,"ASTC 4x4");

    uint32_t qf=0,nq=8; VkQueueFamilyProperties qp[8]; vkGetPhysicalDeviceQueueFamilyProperties(g,&nq,qp); for(uint32_t i=0;i<nq;i++) if(qp[i].queueFlags&VK_QUEUE_GRAPHICS_BIT){ qf=i; break; }
    float pr=1.f; VkDeviceQueueCreateInfo qi={VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qi.queueFamilyIndex=qf; qi.queueCount=1; qi.pQueuePriorities=&pr;
    const char*de[]={VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo di={VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; di.queueCreateInfoCount=1; di.pQueueCreateInfos=&qi; di.enabledExtensionCount=1; di.ppEnabledExtensionNames=de;
    VkDevice dev; CK(vkCreateDevice(g,&di,NULL,&dev)); VkQueue q; vkGetDeviceQueue(dev,qf,0,&q);
    VkCommandPoolCreateInfo cpi={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; cpi.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; cpi.queueFamilyIndex=qf; VkCommandPool pool; CK(vkCreateCommandPool(dev,&cpi,NULL,&pool));
    VkCommandBufferAllocateInfo cai={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; cai.commandPool=pool; cai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount=1; VkCommandBuffer cb; CK(vkAllocateCommandBuffers(dev,&cai,&cb));
    VkFenceCreateInfo fi={VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fi.flags=VK_FENCE_CREATE_SIGNALED_BIT; VkFence fence; CK(vkCreateFence(dev,&fi,NULL,&fence));
    VkSemaphoreCreateInfo si={VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}; VkSemaphore acq,rend[8]; CK(vkCreateSemaphore(dev,&si,NULL,&acq)); for(int i=0;i<8;i++) CK(vkCreateSemaphore(dev,&si,NULL,&rend[i]));

    uint32_t nd=1; VkDisplayPropertiesKHR dp; VkResult dr=vkGetPhysicalDeviceDisplayPropertiesKHR(g,&nd,&dp); if(dr<0||nd<1){ printf("no display\n"); return 1; }
    uint32_t nm=8; VkDisplayModePropertiesKHR mp[8]; vkGetDisplayModePropertiesKHR(g,dp.display,&nm,mp);
    struct { uint32_t w,h; const char*name; float nominal; } want[2]={{720,480,"720x480i",59.94f},{720,240,"720x240p",60.054f}};
    for(int t=0;t<2;t++){
        int mi=-1; for(uint32_t i=0;i<nm;i++) if(mp[i].parameters.visibleRegion.width==want[t].w&&mp[i].parameters.visibleRegion.height==want[t].h) mi=i;
        if(mi<0){ printf("%s not listed\n",want[t].name); continue; }
        VkDisplaySurfaceCreateInfoKHR ds={VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR}; ds.displayMode=mp[mi].displayMode; ds.planeIndex=0; ds.transform=VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR; ds.globalAlpha=1.f; ds.alphaMode=VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR; ds.imageExtent=mp[mi].parameters.visibleRegion;
        VkSurfaceKHR surf; CK(vkCreateDisplayPlaneSurfaceKHR(inst,&ds,NULL,&surf));
        VkBool32 ok=0; vkGetPhysicalDeviceSurfaceSupportKHR(g,qf,surf,&ok);
        VkSurfaceCapabilitiesKHR sc; vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g,surf,&sc);
        uint32_t npm=8; VkPresentModeKHR pm[8]; vkGetPhysicalDeviceSurfacePresentModesKHR(g,surf,&npm,pm);
        printf("\n%s: listed at %.3f Hz, support %u, images %u..%u, present modes:",want[t].name,mp[mi].parameters.refreshRate/1000.0,ok,sc.minImageCount,sc.maxImageCount); for(uint32_t i=0;i<npm;i++) printf(" %d",pm[i]); printf("\n");
        uint32_t nf=8; VkSurfaceFormatKHR sf[8]; vkGetPhysicalDeviceSurfaceFormatsKHR(g,surf,&nf,sf);
        VkSwapchainCreateInfoKHR sw={VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR}; sw.surface=surf; sw.minImageCount=sc.minImageCount<3?3:sc.minImageCount; sw.imageFormat=sf[0].format; sw.imageColorSpace=sf[0].colorSpace; sw.imageExtent=ds.imageExtent; sw.imageArrayLayers=1; sw.imageUsage=VK_IMAGE_USAGE_TRANSFER_DST_BIT; sw.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE; sw.preTransform=VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR; sw.compositeAlpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; sw.presentMode=VK_PRESENT_MODE_FIFO_KHR; sw.clipped=VK_TRUE;
        VkSwapchainKHR chain; CK(vkCreateSwapchainKHR(dev,&sw,NULL,&chain));
        uint32_t ni=8; VkImage img[8]; vkGetSwapchainImagesKHR(dev,chain,&ni,img);
        double t0=nowMs(),prev=0,mn=1e9,mx=0; long frames=0,late=0; double nominalMs=1000.0/want[t].nominal;
        while(nowMs()-t0<5000.0){
            vkWaitForFences(dev,1,&fence,VK_TRUE,UINT64_MAX); vkResetFences(dev,1,&fence);
            uint32_t idx=0; CK(vkAcquireNextImageKHR(dev,chain,UINT64_MAX,acq,VK_NULL_HANDLE,&idx));
            vkResetCommandBuffer(cb,0); VkCommandBufferBeginInfo bi={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; vkBeginCommandBuffer(cb,&bi);
            VkImageSubresourceRange rg={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
            VkImageMemoryBarrier b={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; b.srcAccessMask=0; b.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; b.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED; b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; b.image=img[idx]; b.subresourceRange=rg;
            vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&b);
            VkClearColorValue col; col.float32[0]=t?0.04f:0.04f; col.float32[1]=t?0.20f:0.08f; col.float32[2]=t?0.08f:0.25f; col.float32[3]=1.f; /* blue 480i, green 240p, as tube_lab */
            vkCmdClearColorImage(cb,img[idx],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&col,1,&rg);
            b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; b.dstAccessMask=0; b.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; b.newLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,NULL,0,NULL,1,&b);
            vkEndCommandBuffer(cb);
            VkPipelineStageFlags ws=VK_PIPELINE_STAGE_TRANSFER_BIT; VkSubmitInfo su={VK_STRUCTURE_TYPE_SUBMIT_INFO}; su.waitSemaphoreCount=1; su.pWaitSemaphores=&acq; su.pWaitDstStageMask=&ws; su.commandBufferCount=1; su.pCommandBuffers=&cb; su.signalSemaphoreCount=1; su.pSignalSemaphores=&rend[idx];
            CK(vkQueueSubmit(q,1,&su,fence));
            VkPresentInfoKHR pi={VK_STRUCTURE_TYPE_PRESENT_INFO_KHR}; pi.waitSemaphoreCount=1; pi.pWaitSemaphores=&rend[idx]; pi.swapchainCount=1; pi.pSwapchains=&chain; pi.pImageIndices=&idx; CK(vkQueuePresentKHR(q,&pi));
            double now=nowMs(); if(prev>0){ double gp=now-prev; if(frames>30){ if(gp<mn)mn=gp; if(gp>mx)mx=gp; if(gp>nominalMs*1.5) late++; } } prev=now; frames++;
        }
        double el=nowMs()-t0; printf("[present] %s: %ld presents in %.2f s = %.3f a second; present gap min %.2f max %.2f ms; gaps over 1.5 fields %ld\n",want[t].name,frames,el/1000.0,frames*1000.0/el,mn,mx,late); fflush(stdout);
        vkDeviceWaitIdle(dev); vkDestroySwapchainKHR(dev,chain,NULL); vkDestroySurfaceKHR(inst,surf,NULL);
    }
    vkDestroyDevice(dev,NULL); vkDestroyInstance(inst,NULL);
    printf("[done]\n");
    return 0;
}

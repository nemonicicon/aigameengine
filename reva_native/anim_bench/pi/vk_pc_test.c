/* vk_pc_test.c -- the PC half of the Vulkan road: a Vulkan surface on SDL2's window, a FIFO swapchain, a plain clear, three seconds.
 * It proves the Windows toolchain end to end and reads the present rate on this monitor.
 * Build: gcc -O2 -std=gnu17 -o vk_pc_test.exe vk_pc_test.c -Dmain=SDL_main -lmingw32 -lSDL2main -lSDL2 -lvulkan-1 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#define CK(x) do{ VkResult r_=(x); if(r_!=VK_SUCCESS&&r_!=VK_SUBOPTIMAL_KHR){ printf("FAILED %s: %d\n",#x,r_); exit(1); } }while(0)

int main(int argc,char**argv){
    (void)argc;(void)argv;
    if(SDL_Init(SDL_INIT_VIDEO)){ printf("SDL: %s\n",SDL_GetError()); return 1; }
    SDL_Window*win=SDL_CreateWindow("aigameengine vulkan check",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,640,360,SDL_WINDOW_VULKAN|SDL_WINDOW_SHOWN); if(!win){ printf("window: %s\n",SDL_GetError()); return 1; }
    unsigned ne=0; SDL_Vulkan_GetInstanceExtensions(win,&ne,NULL); const char*ex[8]; SDL_Vulkan_GetInstanceExtensions(win,&ne,ex);
    VkApplicationInfo ai={VK_STRUCTURE_TYPE_APPLICATION_INFO}; ai.apiVersion=VK_API_VERSION_1_3; VkInstanceCreateInfo ci={VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ci.pApplicationInfo=&ai; ci.enabledExtensionCount=ne; ci.ppEnabledExtensionNames=ex; VkInstance inst; CK(vkCreateInstance(&ci,NULL,&inst));
    VkSurfaceKHR surf; if(!SDL_Vulkan_CreateSurface(win,inst,&surf)){ printf("surface: %s\n",SDL_GetError()); return 1; }
    uint32_t n=1; VkPhysicalDevice g; vkEnumeratePhysicalDevices(inst,&n,&g); VkPhysicalDeviceProperties pr; vkGetPhysicalDeviceProperties(g,&pr); printf("%s, Vulkan %u.%u.%u (asked for 1.3)\n",pr.deviceName,VK_VERSION_MAJOR(pr.apiVersion),VK_VERSION_MINOR(pr.apiVersion),VK_VERSION_PATCH(pr.apiVersion));
    uint32_t qf=0,nq=16; VkQueueFamilyProperties qp[16]; vkGetPhysicalDeviceQueueFamilyProperties(g,&nq,qp); for(uint32_t i=0;i<nq;i++){ VkBool32 s=0; vkGetPhysicalDeviceSurfaceSupportKHR(g,i,surf,&s); if(s&&(qp[i].queueFlags&VK_QUEUE_GRAPHICS_BIT)){ qf=i; break; } }
    float p=1.f; VkDeviceQueueCreateInfo qi={VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qi.queueFamilyIndex=qf; qi.queueCount=1; qi.pQueuePriorities=&p; const char*de[]={VK_KHR_SWAPCHAIN_EXTENSION_NAME}; VkDeviceCreateInfo di={VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; di.queueCreateInfoCount=1; di.pQueueCreateInfos=&qi; di.enabledExtensionCount=1; di.ppEnabledExtensionNames=de; VkDevice dev; CK(vkCreateDevice(g,&di,NULL,&dev)); VkQueue q; vkGetDeviceQueue(dev,qf,0,&q);
    VkCommandPoolCreateInfo cpi={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; cpi.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; cpi.queueFamilyIndex=qf; VkCommandPool pool; CK(vkCreateCommandPool(dev,&cpi,NULL,&pool)); VkCommandBufferAllocateInfo cai={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; cai.commandPool=pool; cai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount=1; VkCommandBuffer cb; CK(vkAllocateCommandBuffers(dev,&cai,&cb));
    VkFenceCreateInfo fi={VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fi.flags=VK_FENCE_CREATE_SIGNALED_BIT; VkFence fence; CK(vkCreateFence(dev,&fi,NULL,&fence)); VkSemaphoreCreateInfo si={VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}; VkSemaphore acq,rend[8]; CK(vkCreateSemaphore(dev,&si,NULL,&acq)); for(int i=0;i<8;i++) CK(vkCreateSemaphore(dev,&si,NULL,&rend[i]));
    VkSurfaceCapabilitiesKHR sc; vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g,surf,&sc); uint32_t nf=16; VkSurfaceFormatKHR sf[16]; vkGetPhysicalDeviceSurfaceFormatsKHR(g,surf,&nf,sf); VkSwapchainCreateInfoKHR sw={VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR}; sw.surface=surf; sw.minImageCount=sc.minImageCount<3?3:sc.minImageCount; sw.imageFormat=sf[0].format; sw.imageColorSpace=sf[0].colorSpace; sw.imageExtent=sc.currentExtent; sw.imageArrayLayers=1; sw.imageUsage=VK_IMAGE_USAGE_TRANSFER_DST_BIT; sw.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE; sw.preTransform=sc.currentTransform; sw.compositeAlpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; sw.presentMode=VK_PRESENT_MODE_FIFO_KHR; sw.clipped=1; VkSwapchainKHR chain; CK(vkCreateSwapchainKHR(dev,&sw,NULL,&chain)); uint32_t ni=8; VkImage img[8]; vkGetSwapchainImagesKHR(dev,chain,&ni,img);
    Uint64 f=SDL_GetPerformanceFrequency(),t0=SDL_GetPerformanceCounter(); long frames=0; double mx=0,prev=0;
    for(;;){ double now=(double)(SDL_GetPerformanceCounter()-t0)/f*1000.0; if(now>3000.0) break; SDL_Event e; while(SDL_PollEvent(&e)){}
        vkWaitForFences(dev,1,&fence,VK_TRUE,UINT64_MAX); vkResetFences(dev,1,&fence); uint32_t idx=0; CK(vkAcquireNextImageKHR(dev,chain,UINT64_MAX,acq,VK_NULL_HANDLE,&idx));
        vkResetCommandBuffer(cb,0); VkCommandBufferBeginInfo bi={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; vkBeginCommandBuffer(cb,&bi); VkImageSubresourceRange rg={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}; VkImageMemoryBarrier b={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; b.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; b.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED; b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; b.image=img[idx]; b.subresourceRange=rg; vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&b);
        VkClearColorValue col; col.float32[0]=0.04f; col.float32[1]=0.08f; col.float32[2]=0.20f; col.float32[3]=1.f; vkCmdClearColorImage(cb,img[idx],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&col,1,&rg); b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; b.dstAccessMask=0; b.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; b.newLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,NULL,0,NULL,1,&b); vkEndCommandBuffer(cb);
        VkPipelineStageFlags ws=VK_PIPELINE_STAGE_TRANSFER_BIT; VkSubmitInfo su={VK_STRUCTURE_TYPE_SUBMIT_INFO}; su.waitSemaphoreCount=1; su.pWaitSemaphores=&acq; su.pWaitDstStageMask=&ws; su.commandBufferCount=1; su.pCommandBuffers=&cb; su.signalSemaphoreCount=1; su.pSignalSemaphores=&rend[idx]; CK(vkQueueSubmit(q,1,&su,fence)); VkPresentInfoKHR pi={VK_STRUCTURE_TYPE_PRESENT_INFO_KHR}; pi.waitSemaphoreCount=1; pi.pWaitSemaphores=&rend[idx]; pi.swapchainCount=1; pi.pSwapchains=&chain; pi.pImageIndices=&idx; CK(vkQueuePresentKHR(q,&pi));
        double t=(double)(SDL_GetPerformanceCounter()-t0)/f*1000.0; if(frames>30&&t-prev>mx) mx=t-prev; prev=t; frames++; }
    SDL_DisplayMode dm; SDL_GetCurrentDisplayMode(0,&dm);
    printf("%ld presents in 3 s = %.1f a second, worst gap %.1f ms, display %dx%d at %d Hz\n",frames,frames/3.0,mx,dm.w,dm.h,dm.refresh_rate); fflush(stdout);
    vkDeviceWaitIdle(dev);
    SDL_Quit(); return 0;
}

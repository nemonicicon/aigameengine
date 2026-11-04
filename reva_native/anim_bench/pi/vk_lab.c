/* vk_lab.c -- the same work as v3d_lab2.c, through Vulkan 1.3 on VK_KHR_display: same meshes, same placement, same shading strengths.
 * Dynamic rendering, one pipeline per shader pair built at start, bones in a dynamic uniform ring, depth discarded at the end of the pass.
 * Work is timed from the start of recording to the fence (the same span as glFinish in the GL lab), CPU by the thread's own clock.
 * Build: sh vk_lab_build.sh    Run: sudo ./vk_lab 480i|240p citizen.lab   (console stopped; in-spec modes; dim picture) */
#include "lab_common.h"
#include <vulkan/vulkan.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/select.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

#define CK(x) do{ VkResult r_=(x); if(r_!=VK_SUCCESS&&r_!=VK_SUBOPTIMAL_KHR){ printf("FAILED %s: %d\n",#x,r_); exit(1); } }while(0)
static VkInstance inst; static VkPhysicalDevice gpu; static VkDevice dev; static VkQueue q; static uint32_t qf; static VkCommandPool pool; static VkCommandBuffer cb,cbs[2]; static VkFence fence,fences[2]; static VkSemaphore acq,rend[8];
static VkSwapchainKHR chain; static VkImage swImg[8]; static VkImageView swView[8]; static uint32_t nSw; static VkFormat colorFmt,depthFmt; static int W,H;
static VkImage depthImg,shImg; static VkImageView depthView,shView; static VkPipelineLayout layout; static VkDescriptorSetLayout dsl; static VkDescriptorSet setBody[4],setInst;
static VkPhysicalDeviceMemoryProperties mp; static VkDeviceSize uboAlign;

static uint32_t memType(uint32_t bits,VkMemoryPropertyFlags want){ for(uint32_t i=0;i<mp.memoryTypeCount;i++) if((bits&(1u<<i))&&(mp.memoryTypes[i].propertyFlags&want)==want) return i; printf("no memory type\n"); exit(1); }
typedef struct { VkBuffer b; VkDeviceMemory m; void*map; } Buf;
static Buf mkBuf(VkDeviceSize sz,VkBufferUsageFlags use){ Buf r; VkBufferCreateInfo i={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; i.size=sz; i.usage=use; i.sharingMode=VK_SHARING_MODE_EXCLUSIVE; CK(vkCreateBuffer(dev,&i,NULL,&r.b));
    VkMemoryRequirements mr; vkGetBufferMemoryRequirements(dev,r.b,&mr); VkMemoryAllocateInfo a={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; a.allocationSize=mr.size; a.memoryTypeIndex=memType(mr.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    CK(vkAllocateMemory(dev,&a,NULL,&r.m)); CK(vkBindBufferMemory(dev,r.b,r.m,0)); CK(vkMapMemory(dev,r.m,0,VK_WHOLE_SIZE,0,&r.map)); return r; }
static VkImage mkImg(VkFormat f,uint32_t w,uint32_t h,uint32_t mips,VkImageUsageFlags use,VkImageAspectFlags asp,VkImageView*view){ VkImage img; VkImageCreateInfo i={VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; i.imageType=VK_IMAGE_TYPE_2D; i.format=f; i.extent.width=w; i.extent.height=h; i.extent.depth=1; i.mipLevels=mips; i.arrayLayers=1; i.samples=VK_SAMPLE_COUNT_1_BIT; i.tiling=VK_IMAGE_TILING_OPTIMAL; i.usage=use; CK(vkCreateImage(dev,&i,NULL,&img));
    VkMemoryRequirements mr; vkGetImageMemoryRequirements(dev,img,&mr); VkMemoryAllocateInfo a={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; a.allocationSize=mr.size; a.memoryTypeIndex=memType(mr.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); VkDeviceMemory m; CK(vkAllocateMemory(dev,&a,NULL,&m)); CK(vkBindImageMemory(dev,img,m,0));
    VkImageViewCreateInfo v={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; v.image=img; v.viewType=VK_IMAGE_VIEW_TYPE_2D; v.format=f; v.subresourceRange.aspectMask=asp; v.subresourceRange.levelCount=mips; v.subresourceRange.layerCount=1; CK(vkCreateImageView(dev,&v,NULL,view)); return img; }
static void barrier(VkImage img,VkImageAspectFlags asp,VkImageLayout from,VkImageLayout to,VkPipelineStageFlags s0,VkPipelineStageFlags s1,VkAccessFlags a0,VkAccessFlags a1,uint32_t mips){ VkImageMemoryBarrier b={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; b.srcAccessMask=a0; b.dstAccessMask=a1; b.oldLayout=from; b.newLayout=to; b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; b.image=img; b.subresourceRange.aspectMask=asp; b.subresourceRange.levelCount=mips; b.subresourceRange.layerCount=1; vkCmdPipelineBarrier(cb,s0,s1,0,0,NULL,0,NULL,1,&b); }
static void oneTimeBegin(void){ vkResetCommandBuffer(cb,0); VkCommandBufferBeginInfo bi={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; vkBeginCommandBuffer(cb,&bi); }
static void oneTimeEnd(void){ vkEndCommandBuffer(cb); vkResetFences(dev,1,&fence); VkSubmitInfo su={VK_STRUCTURE_TYPE_SUBMIT_INFO}; su.commandBufferCount=1; su.pCommandBuffers=&cb; CK(vkQueueSubmit(q,1,&su,fence)); vkWaitForFences(dev,1,&fence,VK_TRUE,UINT64_MAX); }
static VkShaderModule shader(const char*name){ char p[256]; snprintf(p,sizeof p,"spv/%s.spv",name); FILE*f=fopen(p,"rb"); if(!f){ printf("no %s\n",p); exit(1); } fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET); uint32_t*c=malloc(n); if(fread(c,1,n,f)!=(size_t)n) exit(1); fclose(f); VkShaderModuleCreateInfo i={VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; i.codeSize=n; i.pCode=c; VkShaderModule m; CK(vkCreateShaderModule(dev,&i,NULL,&m)); free(c); return m; }
enum { PF_DEPTHTEST=1, PF_DEPTHWRITE=2, PF_BLEND=4, PF_SHADOW=8, PF_NOVERT=16 };
static VkPipeline pipeline(const char*vs,const char*fs,int flags){
    VkPipelineShaderStageCreateInfo st[2]={{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}}; st[0].stage=VK_SHADER_STAGE_VERTEX_BIT; st[0].module=shader(vs); st[0].pName="main"; int ns=1; if(fs){ st[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT; st[1].module=shader(fs); st[1].pName="main"; ns=2; }
    VkVertexInputBindingDescription vb={0,sizeof(LV),VK_VERTEX_INPUT_RATE_VERTEX}; VkVertexInputAttributeDescription va[5]={{0,0,VK_FORMAT_R32G32B32_SFLOAT,0},{1,0,VK_FORMAT_R32G32B32_SFLOAT,12},{2,0,VK_FORMAT_R32G32_SFLOAT,24},{3,0,VK_FORMAT_R8G8B8A8_UINT,32},{4,0,VK_FORMAT_R8G8B8A8_UNORM,36}};
    VkPipelineVertexInputStateCreateInfo vi={VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO}; if(!(flags&PF_NOVERT)){ vi.vertexBindingDescriptionCount=1; vi.pVertexBindingDescriptions=&vb; vi.vertexAttributeDescriptionCount=5; vi.pVertexAttributeDescriptions=va; }
    VkPipelineInputAssemblyStateCreateInfo ia={VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; ia.topology=(flags&PF_NOVERT)?VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp={VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; vp.viewportCount=1; vp.scissorCount=1;
    VkPipelineRasterizationStateCreateInfo rs={VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO}; rs.polygonMode=VK_POLYGON_MODE_FILL; rs.cullMode=VK_CULL_MODE_NONE; rs.lineWidth=1.f;
    VkPipelineMultisampleStateCreateInfo ms={VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds={VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO}; ds.depthTestEnable=(flags&PF_DEPTHTEST)?1:0; ds.depthWriteEnable=(flags&PF_DEPTHWRITE)?1:0; ds.depthCompareOp=VK_COMPARE_OP_LESS_OR_EQUAL;
    VkPipelineColorBlendAttachmentState ba={0}; ba.colorWriteMask=15; if(flags&PF_BLEND){ ba.blendEnable=1; ba.srcColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA; ba.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; ba.colorBlendOp=VK_BLEND_OP_ADD; ba.srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE; ba.dstAlphaBlendFactor=VK_BLEND_FACTOR_ZERO; ba.alphaBlendOp=VK_BLEND_OP_ADD; }
    VkPipelineColorBlendStateCreateInfo cbs={VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; cbs.attachmentCount=(flags&PF_SHADOW)?0:1; cbs.pAttachments=&ba;
    VkDynamicState dy[2]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR}; VkPipelineDynamicStateCreateInfo dsi={VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO}; dsi.dynamicStateCount=2; dsi.pDynamicStates=dy;
    VkPipelineRenderingCreateInfo ri={VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO}; if(!(flags&PF_SHADOW)){ ri.colorAttachmentCount=1; ri.pColorAttachmentFormats=&colorFmt; } ri.depthAttachmentFormat=depthFmt;
    VkGraphicsPipelineCreateInfo g={VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO}; g.pNext=&ri; g.stageCount=ns; g.pStages=st; g.pVertexInputState=&vi; g.pInputAssemblyState=&ia; g.pViewportState=&vp; g.pRasterizationState=&rs; g.pMultisampleState=&ms; g.pDepthStencilState=&ds; g.pColorBlendState=&cbs; g.pDynamicState=&dsi; g.layout=layout;
    VkPipeline p; CK(vkCreateGraphicsPipelines(dev,VK_NULL_HANDLE,1,&g,NULL,&p)); return p; }

typedef struct { Buf vb,ib; uint32_t ni; } VMesh;
static VMesh up(LMesh m){ VMesh r; r.ni=m.ni; r.vb=mkBuf(sizeof(LV)*m.nv,VK_BUFFER_USAGE_VERTEX_BUFFER_BIT); memcpy(r.vb.map,m.v,sizeof(LV)*m.nv); r.ib=mkBuf(4*m.ni,VK_BUFFER_USAGE_INDEX_BUFFER_BIT); memcpy(r.ib.map,m.ix,4*m.ni); return r; }
static void upTex(LTex t,float bright,VkImageView*view,VkSampler smp,VkImage*out){ uint32_t mips=1; { int w=t.w,h=t.h; while(w>1||h>1){ w=w>1?w/2:1; h=h>1?h/2:1; mips++; } }
    VkImage img=mkImg(VK_FORMAT_R8G8B8A8_UNORM,t.w,t.h,mips,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,VK_IMAGE_ASPECT_COLOR_BIT,view); *out=img; (void)smp;
    size_t total=0; { int w=t.w,h=t.h; for(uint32_t l=0;l<mips;l++){ total+=(size_t)w*h*4; w=w>1?w/2:1; h=h>1?h/2:1; } } Buf st=mkBuf(total,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    unsigned char*d=malloc((size_t)t.w*t.h*4); for(size_t k=0;k<(size_t)t.w*t.h*4;k++) d[k]=(k%4==3)?t.px[k]:(unsigned char)(t.px[k]*bright);
    oneTimeBegin(); barrier(img,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,VK_ACCESS_TRANSFER_WRITE_BIT,mips);
    { int w=t.w,h=t.h; size_t off=0; unsigned char*cur=d; for(uint32_t l=0;l<mips;l++){ memcpy((char*)st.map+off,cur,(size_t)w*h*4); VkBufferImageCopy c={0}; c.bufferOffset=off; c.imageSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT; c.imageSubresource.mipLevel=l; c.imageSubresource.layerCount=1; c.imageExtent.width=w; c.imageExtent.height=h; c.imageExtent.depth=1; vkCmdCopyBufferToImage(cb,st.b,img,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&c); off+=(size_t)w*h*4; int nw=w,nh=h; unsigned char*nx=labMipDown(cur,w,h,&nw,&nh); free(cur); cur=nx; w=nw; h=nh; if(!cur) break; } }
    barrier(img,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT,mips); oneTimeEnd(); }

/* bones go in a ring, the descriptor's dynamic offset picks a body's; the ring is reset each frame because each frame is waited for */
static Buf ring; static VkDeviceSize ringOff; static VkDeviceSize ringSize=8*1024*1024;
static VkDeviceSize ringBase;
static uint32_t ringPush(const void*d,size_t n){ VkDeviceSize o=ringOff; memcpy((char*)ring.map+o,d,n); ringOff=(o+n+uboAlign-1)&~(uboAlign-1); if(ringOff-ringBase+32768>ringSize/2) ringOff=ringBase; return (uint32_t)o; }

static VkPipeline pStatic,pSkin,pInst,pA,pAs,pB,pBs,pC,pCs,qA,qB,qC,gA,gB,gC,hud,pDepth; static VMesh body800,m100k,m800k,cit,hero,street; static float gt; static VkPipeline curP;
static void bindP(VkPipeline p){ if(curP!=p){ vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,p); curP=p; } }
static void bindMesh(VMesh*m){ VkDeviceSize z=0; vkCmdBindVertexBuffers(cb,0,1,&m->vb.b,&z); vkCmdBindIndexBuffer(cb,m->ib.b,0,VK_INDEX_TYPE_UINT32); }
static void xf(float x,float y,float s,float z){ float v[4]={x,y,s,z}; vkCmdPushConstants(cb,layout,VK_SHADER_STAGE_VERTEX_BIT,0,16,v); }
static void useSet(int t,uint32_t off){ vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&setBody[t],1,&off); }
static void viewport(int x,int y,int w,int h){ VkViewport v={(float)x,(float)y,(float)w,(float)h,0.f,1.f}; VkRect2D s={{x,y},{(uint32_t)w,(uint32_t)h}}; vkCmdSetViewport(cb,0,1,&v); vkCmdSetScissor(cb,0,1,&s); }
static void skinDraw(VMesh*m,int pose,float x,float y,float s,float z,int t){ uint32_t o=ringPush(labBoneV+pose*96*4,96*16); useSet(t,o); xf(x,y,s,z); vkCmdDrawIndexed(cb,m->ni,1,0,0,0); }
static uint32_t zeroBones;
static void statDraw(VMesh*m,float x,float y,float s,float z,int t){ useSet(t,zeroBones); xf(x,y,s,z); vkCmdDrawIndexed(cb,m->ni,1,0,0,0); }
static VkImageView curColor; static VkImage curImg;
static void beginMain(void){ VkRenderingAttachmentInfo c={VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO},d={VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO}; c.imageView=curColor; c.imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; c.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR; c.storeOp=VK_ATTACHMENT_STORE_OP_STORE; c.clearValue.color.float32[0]=0.02f; c.clearValue.color.float32[1]=0.02f; c.clearValue.color.float32[2]=0.04f; c.clearValue.color.float32[3]=1.f;
    d.imageView=depthView; d.imageLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL; d.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR; d.storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE; d.clearValue.depthStencil.depth=1.f;
    VkRenderingInfo r={VK_STRUCTURE_TYPE_RENDERING_INFO}; r.renderArea.extent.width=W; r.renderArea.extent.height=H; r.layerCount=1; r.colorAttachmentCount=1; r.pColorAttachments=&c; r.pDepthAttachment=&d; vkCmdBeginRendering(cb,&r); viewport(0,0,W,H); curP=VK_NULL_HANDLE; }
static void shadowPass(void){ barrier(shImg,VK_IMAGE_ASPECT_DEPTH_BIT,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,1);
    VkRenderingAttachmentInfo d={VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO}; d.imageView=shView; d.imageLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL; d.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR; d.storeOp=VK_ATTACHMENT_STORE_OP_STORE; d.clearValue.depthStencil.depth=1.f; VkRenderingInfo r={VK_STRUCTURE_TYPE_RENDERING_INFO}; r.renderArea.extent.width=1024; r.renderArea.extent.height=1024; r.layerCount=1; r.pDepthAttachment=&d; vkCmdBeginRendering(cb,&r); viewport(0,0,1024,1024); curP=VK_NULL_HANDLE; bindP(pDepth); bindMesh(&cit);
    for(int i=0;i<100;i++){ float x,y,s; labPlace(i,100,&x,&y,&s); skinDraw(&cit,i%8,x+0.03f,y-0.04f,0.16f,0.f,0); } bindMesh(&hero); skinDraw(&hero,0,0.03f,-0.04f,0.16f,-0.1f,0); vkCmdEndRendering(cb);
    barrier(shImg,VK_IMAGE_ASPECT_DEPTH_BIT,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT,1); }
static void citizens(VkPipeline ps){ bindP(ps); bindMesh(&cit); for(int i=0;i<100;i++){ float x,y,s; labPlace(i,100,&x,&y,&s); skinDraw(&cit,i%8,x,y,0.16f,0.f,0); } bindMesh(&hero); skinDraw(&hero,0,0.f,0.f,0.16f,-0.1f,0); }
static void streetDraw(VkPipeline ps){ bindP(ps); bindMesh(&street); for(int i=0;i<100;i++){ float x,y,s; labPlace(i,100,&x,&y,&s); statDraw(&street,x+0.05f,y+0.04f,s,0.15f,i%4); } }
static void groundAndHud(VkPipeline g){ bindP(g); useSet(1,zeroBones); vkCmdDraw(cb,4,1,0,0); bindP(hud); useSet(2,zeroBones); vkCmdDraw(cb,4,1,0,0); vkCmdDraw(cb,4,1,0,0); }

typedef enum { T_EMPTY,T_GRID100,T_GRID800,T_DRAWS,T_BODIES,T_INST,T_LAYER,T_CIT,T_PROOF,T_SPLIT } TT;
static int kmsRoad=0;
static void work(TT t,int a){ labBones(gt); ringOff=ringBase; zeroBones=ringPush(labBoneV,96*16);
    if((t==T_PROOF||t==T_SPLIT)&&a>0) shadowPass();
    barrier(curImg,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,1);
    barrier(depthImg,VK_IMAGE_ASPECT_DEPTH_BIT,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,0,VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,1);
    beginMain();
    switch(t){ case T_EMPTY: break;
      case T_GRID100: bindP(pSkin); bindMesh(&m100k); skinDraw(&m100k,0,0,0,0.5f,0,0); break;
      case T_GRID800: bindP(pSkin); bindMesh(&m800k); skinDraw(&m800k,0,0,0,0.5f,0,0); break;
      case T_DRAWS: bindP(pStatic); bindMesh(&body800); for(int i=0;i<a;i++){ float x,y,s; labPlace(i,a,&x,&y,&s); statDraw(&body800,x,y,s,0,0); } break;
      case T_BODIES: bindP(pSkin); bindMesh(&body800); for(int i=0;i<a;i++){ float x,y,s; labPlace(i,a,&x,&y,&s); skinDraw(&body800,i%8,x,y,s,0,0); } break;
      case T_INST: { static float data[768*4+1600]; memcpy(data,labBoneV,sizeof labBoneV); float*in=data+768*4; for(int i=0;i<a;i++){ float x,y,s; labPlace(i,a,&x,&y,&s); in[i*4]=x; in[i*4+1]=y; in[i*4+2]=s; in[i*4+3]=(float)(i%8); } uint32_t o=ringPush(data,sizeof data); bindP(pInst); bindMesh(&body800); vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&setInst,1,&o); vkCmdDrawIndexed(cb,body800.ni,a,0,0,0); } break;
      case T_LAYER: bindP(a==0?qA:a==1?qB:qC); useSet(1,zeroBones); vkCmdDraw(cb,4,1,0,0); break;
      case T_CIT: citizens(a==0?pA:a==1?pB:pC); break;
      case T_PROOF: citizens(a==0?pA:a==1?pB:pC); streetDraw(a==0?pAs:a==1?pBs:pCs); groundAndHud(a==0?gA:a==1?gB:gC); break;
      case T_SPLIT: for(int v=0;v<4;v++){ viewport((v&1)*W/2,(v>>1)*H/2,W/2,H/2); citizens(a==0?pA:pB); streetDraw(a==0?pAs:pBs); } viewport(0,0,W,H); groundAndHud(a==0?gA:gB); break; }
    vkCmdEndRendering(cb);
    barrier(curImg,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,kmsRoad?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,0,1); }
static void run(const char*name,TT t,int a){
    static double w[120],c[120]; int n=0;
    for(int i=0;i<68;i++){ if(i==8) labGpuSnap(0); uint32_t idx=0; CK(vkAcquireNextImageKHR(dev,chain,UINT64_MAX,acq,VK_NULL_HANDLE,&idx)); curColor=swView[idx]; curImg=swImg[idx];
        double t0=labMs(CLOCK_MONOTONIC),c0=labMs(CLOCK_THREAD_CPUTIME_ID); gt+=1.0f/60;
        vkResetFences(dev,1,&fence); vkResetCommandBuffer(cb,0); VkCommandBufferBeginInfo bi={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; vkBeginCommandBuffer(cb,&bi); work(t,a); vkEndCommandBuffer(cb);
        VkPipelineStageFlags ws=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; VkSubmitInfo su={VK_STRUCTURE_TYPE_SUBMIT_INFO}; su.waitSemaphoreCount=1; su.pWaitSemaphores=&acq; su.pWaitDstStageMask=&ws; su.commandBufferCount=1; su.pCommandBuffers=&cb; su.signalSemaphoreCount=1; su.pSignalSemaphores=&rend[idx]; CK(vkQueueSubmit(q,1,&su,fence)); vkWaitForFences(dev,1,&fence,VK_TRUE,UINT64_MAX);
        double dw=labMs(CLOCK_MONOTONIC)-t0,dc=labMs(CLOCK_THREAD_CPUTIME_ID)-c0; if(i>=8) labGpuSnap(1);
        VkPresentInfoKHR pi={VK_STRUCTURE_TYPE_PRESENT_INFO_KHR}; pi.waitSemaphoreCount=1; pi.pWaitSemaphores=&rend[idx]; pi.swapchainCount=1; pi.pSwapchains=&chain; pi.pImageIndices=&idx; CK(vkQueuePresentKHR(q,&pi));
        if(i>=8){ w[n]=dw; c[n]=dc; n++; } if(dw>300.0&&i>=12) break; }
    labReport(name,w,c,n);
}
/* ---- THE DIRECT ROAD: three dumb scanout buffers on the display card, imported as linear images. We render straight into them and flip them
 * ourselves, as the GL build does: no copy, and the kernel's flip timestamps are ours. A frame's flip waits for that frame's fence, and the next
 * frame records meanwhile, so two frames overlap. */
static int kfd; static uint32_t kCrtc,kConn; static drmModeModeInfo kMode; static uint32_t kFb[3]; static int kFirst=1,kPending=0;
static void kFlip(int f,unsigned a,unsigned b,unsigned c,void*d){ (void)f;(void)a;(void)d; labFlipMark(b,c); kPending=0; }
static drmEventContext kEv;
static void kWaitFlip(void){ while(kPending){ fd_set s; FD_ZERO(&s); FD_SET(kfd,&s); struct timeval tv={1,0}; if(select(kfd+1,&s,NULL,NULL,&tv)>0) drmHandleEvent(kfd,&kEv); else break; } }
static void kShow(int i){ if(kFirst){ if(drmModeSetCrtc(kfd,kCrtc,kFb[i],0,0,&kConn,1,&kMode)){ perror("setcrtc"); exit(1); } kFirst=0; return; } kWaitFlip(); kPending=1; if(drmModePageFlip(kfd,kCrtc,kFb[i],DRM_MODE_PAGE_FLIP_EVENT,NULL)) kPending=0; }
static void kmsInit(int want240){
    kfd=open("/dev/dri/card1",O_RDWR|O_CLOEXEC); if(kfd<0){ perror("card1"); exit(1); } drmModeRes*res=drmModeGetResources(kfd); drmModeConnector*c=NULL;
    for(int i=0;i<res->count_connectors;i++){ drmModeConnector*k=drmModeGetConnector(kfd,res->connectors[i]); if(k&&k->connector_type==DRM_MODE_CONNECTOR_Composite){ c=k; break; } if(k) drmModeFreeConnector(k); } if(!c){ printf("no composite\n"); exit(1); }
    int got=0; for(int i=0;i<c->count_modes;i++){ drmModeModeInfo*m=&c->modes[i]; int il=(m->flags&DRM_MODE_FLAG_INTERLACE)!=0; if(m->hdisplay==720&&((want240&&m->vdisplay==240&&!il)||(!want240&&m->vdisplay==480&&il))){ kMode=*m; got=1; } } if(!got){ printf("no mode\n"); exit(1); }
    labFieldMs=(double)kMode.htotal*kMode.vtotal/kMode.clock/((kMode.flags&DRM_MODE_FLAG_INTERLACE)?2.0:1.0); kConn=c->connector_id;
    if(c->encoder_id){ drmModeEncoder*e=drmModeGetEncoder(kfd,c->encoder_id); if(e){ kCrtc=e->crtc_id; drmModeFreeEncoder(e);} } if(!kCrtc){ for(int i=0;i<c->count_encoders&&!kCrtc;i++){ drmModeEncoder*e=drmModeGetEncoder(kfd,c->encoders[i]); if(!e) continue; for(int j=0;j<res->count_crtcs;j++) if(e->possible_crtcs&(1u<<j)){ kCrtc=res->crtcs[j]; break; } drmModeFreeEncoder(e); } }
    memset(&kEv,0,sizeof kEv); kEv.version=2; kEv.page_flip_handler=kFlip;
    PFN_vkGetMemoryFdPropertiesKHR gfp=(PFN_vkGetMemoryFdPropertiesKHR)vkGetDeviceProcAddr(dev,"vkGetMemoryFdPropertiesKHR");
    for(int i=0;i<3;i++){ struct drm_mode_create_dumb cd; memset(&cd,0,sizeof cd); cd.width=W; cd.height=H; cd.bpp=32; if(drmIoctl(kfd,DRM_IOCTL_MODE_CREATE_DUMB,&cd)){ perror("dumb"); exit(1); } if(drmModeAddFB(kfd,W,H,24,32,cd.pitch,cd.handle,&kFb[i])){ perror("addfb"); exit(1); } int pfd=-1; if(drmPrimeHandleToFD(kfd,cd.handle,O_RDWR|O_CLOEXEC,&pfd)){ perror("prime"); exit(1); }
        VkSubresourceLayout pl={0}; pl.size=cd.size; pl.rowPitch=cd.pitch; VkImageDrmFormatModifierExplicitCreateInfoEXT md={VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT}; md.drmFormatModifier=DRM_FORMAT_MOD_LINEAR; md.drmFormatModifierPlaneCount=1; md.pPlaneLayouts=&pl; VkExternalMemoryImageCreateInfo em={VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO}; em.pNext=&md; em.handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
        VkImageCreateInfo ii={VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; ii.pNext=&em; ii.imageType=VK_IMAGE_TYPE_2D; ii.format=VK_FORMAT_B8G8R8A8_UNORM; ii.extent.width=W; ii.extent.height=H; ii.extent.depth=1; ii.mipLevels=1; ii.arrayLayers=1; ii.samples=VK_SAMPLE_COUNT_1_BIT; ii.tiling=VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT; ii.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; CK(vkCreateImage(dev,&ii,NULL,&swImg[i]));
        VkMemoryRequirements mr; vkGetImageMemoryRequirements(dev,swImg[i],&mr); VkMemoryFdPropertiesKHR fp={VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR}; CK(gfp(dev,VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,pfd,&fp)); VkMemoryDedicatedAllocateInfo dd={VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO}; dd.image=swImg[i]; VkImportMemoryFdInfoKHR im={VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR}; im.pNext=&dd; im.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT; im.fd=pfd; VkMemoryAllocateInfo ma={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ma.pNext=&im; ma.allocationSize=mr.size; ma.memoryTypeIndex=memType(mr.memoryTypeBits&fp.memoryTypeBits,0); VkDeviceMemory m; CK(vkAllocateMemory(dev,&ma,NULL,&m)); CK(vkBindImageMemory(dev,swImg[i],m,0));
        VkImageViewCreateInfo v={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; v.image=swImg[i]; v.viewType=VK_IMAGE_VIEW_TYPE_2D; v.format=VK_FORMAT_B8G8R8A8_UNORM; v.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT; v.subresourceRange.levelCount=1; v.subresourceRange.layerCount=1; CK(vkCreateImageView(dev,&v,NULL,&swView[i])); }
    nSw=3; colorFmt=VK_FORMAT_B8G8R8A8_UNORM; printf("[lab] the direct road: 3 linear scanout buffers, flips ours, field %.2f ms\n",labFieldMs);
}
static int kIdx=0;
static void runK(const char*name,TT t,int a){   /* the work test on the direct road: one frame at a time, waited for */
    static double w[120],c[120]; int n=0;
    for(int i=0;i<68;i++){ if(i==8) labGpuSnap(0); int idx=kIdx; kIdx=(kIdx+1)%3; curColor=swView[idx]; curImg=swImg[idx]; double t0=labMs(CLOCK_MONOTONIC),c0=labMs(CLOCK_THREAD_CPUTIME_ID); gt+=1.0f/60; cb=cbs[0]; vkResetFences(dev,1,&fence); vkResetCommandBuffer(cb,0); VkCommandBufferBeginInfo bi={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; vkBeginCommandBuffer(cb,&bi); work(t,a); vkEndCommandBuffer(cb); VkSubmitInfo su={VK_STRUCTURE_TYPE_SUBMIT_INFO}; su.commandBufferCount=1; su.pCommandBuffers=&cb; CK(vkQueueSubmit(q,1,&su,fence)); vkWaitForFences(dev,1,&fence,VK_TRUE,UINT64_MAX); double dw=labMs(CLOCK_MONOTONIC)-t0,dc=labMs(CLOCK_THREAD_CPUTIME_ID)-c0; if(i>=8) labGpuSnap(1); kShow(idx); if(i>=8){ w[n]=dw; c[n]=dc; n++; } if(dw>300.0&&i>=12) break; }
    labReport(name,w,c,n);
}
static void liveK(const char*name,TT t,int a){   /* LIVE: two frames overlap, each flip waits for its own frame's fence */
    int prevIdx=-1,prevSlot=-1; double cpu=0.0; int N=30+(getenv("LAB_LIVEONLY")?atoi(getenv("LAB_LIVEONLY")):300); double t0=0.0;
    for(int i=0;i<N;i++){ if(i==30){ vkWaitForFences(dev,1,&fences[1],VK_TRUE,UINT64_MAX); labLiveStart(); t0=labMs(CLOCK_MONOTONIC); cpu=0.0; }
        int slot=i&1; vkWaitForFences(dev,1,&fences[slot],VK_TRUE,UINT64_MAX); double c0=labMs(CLOCK_THREAD_CPUTIME_ID); int idx=kIdx; kIdx=(kIdx+1)%3; curColor=swView[idx]; curImg=swImg[idx]; gt+=1.0f/60; cb=cbs[slot]; ringBase=slot?ringSize/2:0; vkResetFences(dev,1,&fences[slot]); vkResetCommandBuffer(cb,0); VkCommandBufferBeginInfo bi={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; vkBeginCommandBuffer(cb,&bi); work(t,a); vkEndCommandBuffer(cb); VkSubmitInfo su={VK_STRUCTURE_TYPE_SUBMIT_INFO}; su.commandBufferCount=1; su.pCommandBuffers=&cb; CK(vkQueueSubmit(q,1,&su,fences[slot])); cpu+=labMs(CLOCK_THREAD_CPUTIME_ID)-c0;
        if(prevSlot>=0){ vkWaitForFences(dev,1,&fences[prevSlot],VK_TRUE,UINT64_MAX); kShow(prevIdx); } prevIdx=idx; prevSlot=slot; }
    vkDeviceWaitIdle(dev); ringBase=0; labLiveReport(name,(labMs(CLOCK_MONOTONIC)-t0)/1000.0,cpu/(N-30));
}
int main(int argc,char**argv){
    kmsRoad=(argc>3&&!strcmp(argv[3],"kms"));
    int want240=(argc>1&&!strcmp(argv[1],"240p")); LMesh cm; LTex ct; if(argc<3||!labLoad(argv[2],&cm,&ct)) return 1;
    const char*ie[]={VK_KHR_SURFACE_EXTENSION_NAME,"VK_KHR_display"}; VkApplicationInfo ai={VK_STRUCTURE_TYPE_APPLICATION_INFO}; ai.apiVersion=VK_API_VERSION_1_3; VkInstanceCreateInfo ci={VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ci.pApplicationInfo=&ai; ci.enabledExtensionCount=kmsRoad?0:2; ci.ppEnabledExtensionNames=ie; CK(vkCreateInstance(&ci,NULL,&inst));
    uint32_t n=8; VkPhysicalDevice pd[8]; vkEnumeratePhysicalDevices(inst,&n,pd); for(uint32_t i=0;i<n;i++){ VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(pd[i],&p); if(strstr(p.deviceName,"V3D")){ gpu=pd[i]; uboAlign=p.limits.minUniformBufferOffsetAlignment; printf("[lab] VK %s %s, Vulkan %u.%u, ubo align %u\n",want240?"240p":"480i",p.deviceName,VK_VERSION_MAJOR(p.apiVersion),VK_VERSION_MINOR(p.apiVersion),(unsigned)uboAlign); } } if(!gpu) return 1; vkGetPhysicalDeviceMemoryProperties(gpu,&mp);
    uint32_t nq=8; VkQueueFamilyProperties qp[8]; vkGetPhysicalDeviceQueueFamilyProperties(gpu,&nq,qp); for(uint32_t i=0;i<nq;i++) if(qp[i].queueFlags&VK_QUEUE_GRAPHICS_BIT){ qf=i; break; }
    float pr=1.f; VkDeviceQueueCreateInfo qi={VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qi.queueFamilyIndex=qf; qi.queueCount=1; qi.pQueuePriorities=&pr; const char*de[]={VK_KHR_SWAPCHAIN_EXTENSION_NAME}; const char*deK[]={"VK_KHR_external_memory_fd","VK_EXT_external_memory_dma_buf","VK_EXT_image_drm_format_modifier"}; VkPhysicalDeviceVulkan13Features f13={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES}; f13.dynamicRendering=1; f13.synchronization2=1;
    VkDeviceCreateInfo di={VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; di.pNext=&f13; di.queueCreateInfoCount=1; di.pQueueCreateInfos=&qi; di.enabledExtensionCount=kmsRoad?3:1; di.ppEnabledExtensionNames=kmsRoad?deK:de; CK(vkCreateDevice(gpu,&di,NULL,&dev)); vkGetDeviceQueue(dev,qf,0,&q);
    VkCommandPoolCreateInfo cpi={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; cpi.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; cpi.queueFamilyIndex=qf; CK(vkCreateCommandPool(dev,&cpi,NULL,&pool)); VkCommandBufferAllocateInfo cai={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; cai.commandPool=pool; cai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount=2; CK(vkAllocateCommandBuffers(dev,&cai,cbs)); cb=cbs[0];
    VkFenceCreateInfo fi={VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; CK(vkCreateFence(dev,&fi,NULL,&fence)); { VkFenceCreateInfo fs={VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fs.flags=VK_FENCE_CREATE_SIGNALED_BIT; CK(vkCreateFence(dev,&fs,NULL,&fences[0])); CK(vkCreateFence(dev,&fs,NULL,&fences[1])); } VkSemaphoreCreateInfo si={VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}; CK(vkCreateSemaphore(dev,&si,NULL,&acq)); for(int i=0;i<8;i++) CK(vkCreateSemaphore(dev,&si,NULL,&rend[i]));
    W=720; H=want240?240:480;
    if(kmsRoad) kmsInit(want240); else {
    uint32_t nd=1; VkDisplayPropertiesKHR dp; VkResult dr=vkGetPhysicalDeviceDisplayPropertiesKHR(gpu,&nd,&dp); if(dr<0||nd<1){ printf("no display\n"); return 1; } uint32_t nm=8; VkDisplayModePropertiesKHR mdp[8]; vkGetDisplayModePropertiesKHR(gpu,dp.display,&nm,mdp); W=720; H=want240?240:480; int mi=-1; for(uint32_t i=0;i<nm;i++) if((int)mdp[i].parameters.visibleRegion.width==W&&(int)mdp[i].parameters.visibleRegion.height==H) mi=i; if(mi<0){ printf("mode not listed\n"); return 1; }
    VkDisplaySurfaceCreateInfoKHR ds={VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR}; ds.displayMode=mdp[mi].displayMode; ds.transform=VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR; ds.globalAlpha=1.f; ds.alphaMode=VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR; ds.imageExtent=mdp[mi].parameters.visibleRegion; VkSurfaceKHR surf; CK(vkCreateDisplayPlaneSurfaceKHR(inst,&ds,NULL,&surf)); VkBool32 sup=0; vkGetPhysicalDeviceSurfaceSupportKHR(gpu,qf,surf,&sup);
    uint32_t nf=8; VkSurfaceFormatKHR sf[8]; vkGetPhysicalDeviceSurfaceFormatsKHR(gpu,surf,&nf,sf); colorFmt=sf[0].format; /* ⛔ the first format listed is sRGB, which doubles tile memory on V3D: take plain 8-bit, as the GL lab's scanout is */ for(uint32_t i=0;i<nf;i++) if(sf[i].format==VK_FORMAT_B8G8R8A8_UNORM) colorFmt=sf[i].format; VkSwapchainCreateInfoKHR sw={VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR}; sw.surface=surf; sw.minImageCount=3; sw.imageFormat=colorFmt; sw.imageColorSpace=sf[0].colorSpace; sw.imageExtent=ds.imageExtent; sw.imageArrayLayers=1; sw.imageUsage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; sw.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE; sw.preTransform=VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR; sw.compositeAlpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; sw.presentMode=VK_PRESENT_MODE_FIFO_KHR; sw.clipped=1; CK(vkCreateSwapchainKHR(dev,&sw,NULL,&chain)); nSw=8; vkGetSwapchainImagesKHR(dev,chain,&nSw,swImg);
    for(uint32_t i=0;i<nSw;i++){ VkImageViewCreateInfo v={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; v.image=swImg[i]; v.viewType=VK_IMAGE_VIEW_TYPE_2D; v.format=colorFmt; v.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT; v.subresourceRange.levelCount=1; v.subresourceRange.layerCount=1; CK(vkCreateImageView(dev,&v,NULL,&swView[i])); }
    }
    { VkFormat tryF[3]={VK_FORMAT_X8_D24_UNORM_PACK32,VK_FORMAT_D24_UNORM_S8_UINT,VK_FORMAT_D32_SFLOAT}; for(int i=0;i<3&&!depthFmt;i++){ VkFormatProperties p; vkGetPhysicalDeviceFormatProperties(gpu,tryF[i],&p); if((p.optimalTilingFeatures&VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)&&(p.optimalTilingFeatures&VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) depthFmt=tryF[i]; } printf("[lab] colour format %d, depth format %d\n",colorFmt,depthFmt); }
    depthImg=mkImg(depthFmt,W,H,1,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,VK_IMAGE_ASPECT_DEPTH_BIT,&depthView); shImg=mkImg(depthFmt,1024,1024,1,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,VK_IMAGE_ASPECT_DEPTH_BIT,&shView);
    VkSamplerCreateInfo sci={VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO}; sci.magFilter=VK_FILTER_LINEAR; sci.minFilter=VK_FILTER_LINEAR; sci.mipmapMode=VK_SAMPLER_MIPMAP_MODE_LINEAR; sci.addressModeU=sci.addressModeV=sci.addressModeW=VK_SAMPLER_ADDRESS_MODE_REPEAT; sci.maxLod=VK_LOD_CLAMP_NONE; VkSampler smp; CK(vkCreateSampler(dev,&sci,NULL,&smp)); sci.compareEnable=1; sci.compareOp=VK_COMPARE_OP_LESS_OR_EQUAL; sci.addressModeU=sci.addressModeV=sci.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; sci.maxLod=0.f; VkSampler smpSh; CK(vkCreateSampler(dev,&sci,NULL,&smpSh));
    VkDescriptorSetLayoutBinding bd[4]={{0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,1,VK_SHADER_STAGE_VERTEX_BIT,NULL},{1,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,NULL},{2,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,NULL},{3,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,NULL}}; VkDescriptorSetLayoutCreateInfo dl={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; dl.bindingCount=4; dl.pBindings=bd; CK(vkCreateDescriptorSetLayout(dev,&dl,NULL,&dsl));
    VkPushConstantRange pc={VK_SHADER_STAGE_VERTEX_BIT,0,16}; VkPipelineLayoutCreateInfo pli={VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; pli.setLayoutCount=1; pli.pSetLayouts=&dsl; pli.pushConstantRangeCount=1; pli.pPushConstantRanges=&pc; CK(vkCreatePipelineLayout(dev,&pli,NULL,&layout));
    ring=mkBuf(ringSize,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT); Buf lights=mkBuf(192,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT); { float lp[12][4]={{-0.5f,0.5f,0.4f,0},{0.5f,0.5f,0.4f,0},{-0.5f,-0.5f,0.4f,0},{0.5f,-0.5f,0.4f,0},{0,0,0.5f,0},{0,0.8f,0.3f,0},{0.1f,0.05f,0.02f,0},{0.02f,0.05f,0.1f,0},{0.05f,0.1f,0.02f,0},{0.1f,0.02f,0.05f,0},{0.05f,0.05f,0.05f,0},{0.02f,0.02f,0.1f,0}}; memcpy(lights.map,lp,sizeof lp); }
    VkDescriptorPoolSize ps[3]={{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,8},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,16},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,8}}; VkDescriptorPoolCreateInfo dpi={VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; dpi.maxSets=8; dpi.poolSizeCount=3; dpi.pPoolSizes=ps; VkDescriptorPool dp2; CK(vkCreateDescriptorPool(dev,&dpi,NULL,&dp2));
    VkImageView tv[4]; VkImage ti[4]; for(int i=0;i<4;i++) upTex(ct,0.5f+0.1f*i,&tv[i],smp,&ti[i]);
    oneTimeBegin(); barrier(shImg,VK_IMAGE_ASPECT_DEPTH_BIT,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,VK_ACCESS_SHADER_READ_BIT,1); oneTimeEnd();
    for(int k=0;k<5;k++){ VkDescriptorSetAllocateInfo a={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; a.descriptorPool=dp2; a.descriptorSetCount=1; a.pSetLayouts=&dsl; VkDescriptorSet s; CK(vkAllocateDescriptorSets(dev,&a,&s)); if(k<4) setBody[k]=s; else setInst=s;
        VkDescriptorBufferInfo bu={ring.b,0,k<4?96*16:(768*16+400*16)}; VkDescriptorImageInfo im={smp,tv[k<4?k:0],VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}; VkDescriptorImageInfo sh={smpSh,shView,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}; VkDescriptorBufferInfo lu={lights.b,0,192};
        VkWriteDescriptorSet wr[4]={{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET},{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET},{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET},{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}}; for(int w=0;w<4;w++){ wr[w].dstSet=s; wr[w].dstBinding=w; wr[w].descriptorCount=1; } wr[0].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC; wr[0].pBufferInfo=&bu; wr[1].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; wr[1].pImageInfo=&im; wr[2].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; wr[2].pImageInfo=&sh; wr[3].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; wr[3].pBufferInfo=&lu; vkUpdateDescriptorSets(dev,4,wr,0,NULL); }
    double tp=labMs(CLOCK_MONOTONIC);
    int D=PF_DEPTHTEST|PF_DEPTHWRITE; pStatic=pipeline("static_v","lit_f",D); pSkin=pipeline("skin_v","lit_f",D); pInst=pipeline("inst_v","lit_f",D); pA=pipeline("skin_v","a_f",D); pAs=pipeline("static_v","a_f",D); pB=pipeline("skin_v","b_f",D); pBs=pipeline("static_v","b_f",D); pC=pipeline("skin_v","c_f",D); pCs=pipeline("static_v","c_f",D);
    qA=pipeline("quad_v","a_f",PF_NOVERT); qB=pipeline("quad_v","b_f",PF_NOVERT); qC=pipeline("quad_v","c_f",PF_NOVERT); gA=pipeline("quad_v","a_f",PF_NOVERT|PF_DEPTHTEST); gB=pipeline("quad_v","b_f",PF_NOVERT|PF_DEPTHTEST); gC=pipeline("quad_v","c_f",PF_NOVERT|PF_DEPTHTEST); hud=pipeline("quad_v","a_f",PF_NOVERT|PF_BLEND); pDepth=pipeline("skin_v",NULL,D|PF_SHADOW);
    printf("[lab] 17 pipelines built in %.0f ms\n",labMs(CLOCK_MONOTONIC)-tp);
    body800=up(labGrid(20)); m100k=up(labGrid(129)); m800k=up(labGrid(365)); cit=up(cm); hero=up(labGrid(100)); street=up(labGrid(18));
    if(kmsRoad){
    if(!getenv("LAB_LIVEONLY")){
    runK("empty",T_EMPTY,0); runK("grid skin 800k",T_GRID800,0); runK("draws 800 static",T_DRAWS,800); runK("bodies 400 own bones",T_BODIES,400); runK("instanced 400",T_INST,400);
    runK("full-screen layer A",T_LAYER,0); runK("full-screen layer B",T_LAYER,1); runK("full-screen layer C",T_LAYER,2); runK("citizens+hero A",T_CIT,0); runK("citizens+hero B",T_CIT,1); runK("citizens+hero C",T_CIT,2);
    runK("PROOF FRAME A",T_PROOF,0); runK("PROOF FRAME B + shadow",T_PROOF,1); runK("PROOF FRAME C + shadow",T_PROOF,2); runK("SPLIT x4 A",T_SPLIT,0); runK("SPLIT x4 B + shadow",T_SPLIT,1); }
    liveK("PROOF A",T_PROOF,0); liveK("PROOF B + shadow",T_PROOF,1); liveK("PROOF C + shadow",T_PROOF,2); liveK("SPLIT x4 A",T_SPLIT,0); liveK("SPLIT x4 B + shadow",T_SPLIT,1);
    vkDeviceWaitIdle(dev); printf("[done]\n"); return 0; }
    run("empty",T_EMPTY,0); run("grid skin 100k",T_GRID100,0); run("grid skin 800k",T_GRID800,0); run("draws 800 static",T_DRAWS,800);
    run("bodies 100 own bones",T_BODIES,100); run("bodies 400 own bones",T_BODIES,400); run("instanced 100",T_INST,100); run("instanced 400",T_INST,400);
    run("full-screen layer A",T_LAYER,0); run("full-screen layer B",T_LAYER,1); run("full-screen layer C",T_LAYER,2);
    run("citizens+hero A",T_CIT,0); run("citizens+hero B",T_CIT,1); run("citizens+hero C",T_CIT,2);
    run("PROOF FRAME A",T_PROOF,0); run("PROOF FRAME B + shadow",T_PROOF,1); run("PROOF FRAME C + shadow",T_PROOF,2);
    run("SPLIT x4 A",T_SPLIT,0); run("SPLIT x4 B + shadow",T_SPLIT,1);
    vkDeviceWaitIdle(dev); printf("[done]\n"); return 0;
}

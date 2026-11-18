#include "gpu.hpp"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <stb_image_write.h>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <fstream>

std::atomic<int> validationWarnings{},validationErrors{};
static VKAPI_ATTR VkBool32 VKAPI_CALL onDebug(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,const VkDebugUtilsMessengerCallbackDataEXT* data,void*) {
    if(severity>=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ++validationErrors;
    else ++validationWarnings;
    std::cerr << data->pMessage << '\n'; return VK_FALSE;
}
void Gpu::init(int width,int height,bool interactive,bool validate,bool sync) {
    validation=validate; vsync=sync; size={uint32_t(width),uint32_t(height)};
    check(volkInitialize());
    std::vector<const char*> extensions;
    if(interactive) {
        if(!glfwInit()) throw std::runtime_error("GLFW initialization failed");
        life.add([]{glfwTerminate();});
        glfwWindowHint(GLFW_CLIENT_API,GLFW_NO_API);
        window=glfwCreateWindow(width,height,"Voxelith",nullptr,nullptr);
        if(!window) throw std::runtime_error("Window creation failed");
        life.add([this]{glfwDestroyWindow(window);});
        uint32_t n{}; auto names=glfwGetRequiredInstanceExtensions(&n);
        if(!names) throw std::runtime_error("Window system lacks Vulkan support");
        extensions.assign(names,names+n);
    }
    const char* layer="VK_LAYER_KHRONOS_validation";
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.pApplicationName="Voxelith"; app.apiVersion=VK_API_VERSION_1_3;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ci.pApplicationInfo=&app;
    if(validation){ extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME); ci.enabledLayerCount=1; ci.ppEnabledLayerNames=&layer; }
    ci.enabledExtensionCount=uint32_t(extensions.size()); ci.ppEnabledExtensionNames=extensions.data();
    check(vkCreateInstance(&ci,nullptr,&instance)); volkLoadInstance(instance);
    life.add([this]{vkDestroyInstance(instance,nullptr);});
    if(validation){
        VkDebugUtilsMessengerCreateInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        info.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        info.messageType=7; info.pfnUserCallback=onDebug;
        VkDebugUtilsMessengerEXT messenger; check(vkCreateDebugUtilsMessengerEXT(instance,&info,nullptr,&messenger));
        life.add([=]{vkDestroyDebugUtilsMessengerEXT(instance,messenger,nullptr);});
    }
    if(window){ check(glfwCreateWindowSurface(instance,window,nullptr,&surface)); life.add([this]{vkDestroySurfaceKHR(instance,surface,nullptr);}); }
    uint32_t n=0; check(vkEnumeratePhysicalDevices(instance,&n,nullptr)); std::vector<VkPhysicalDevice> devices(n);
    check(vkEnumeratePhysicalDevices(instance,&n,devices.data())); int best=-1;
    for(auto candidate: devices){
        VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(candidate,&p);
        VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceFeatures2 feats{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,&f13}; vkGetPhysicalDeviceFeatures2(candidate,&feats);
        if(p.apiVersion<VK_API_VERSION_1_3 || !f13.dynamicRendering || !f13.synchronization2) continue;
        uint32_t count; vkGetPhysicalDeviceQueueFamilyProperties(candidate,&count,nullptr);
        std::vector<VkQueueFamilyProperties> props(count); vkGetPhysicalDeviceQueueFamilyProperties(candidate,&count,props.data());
        for(uint32_t i=0;i<count;i++){
            VkBool32 present=VK_TRUE; if(surface) check(vkGetPhysicalDeviceSurfaceSupportKHR(candidate,i,surface,&present));
            if(!present || (props[i].queueFlags & 3)!=3 || !props[i].timestampValidBits) continue;
            int score=p.deviceType==VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2:1;
            if(score>best){best=score; physical=candidate; family=i; properties=p;}
        }
    }
    if(!physical) throw std::runtime_error("Requires Vulkan 1.3 graphics/compute device with timestamps");
    float priority=1;
    VkDeviceQueueCreateInfo qc{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qc.queueFamilyIndex=family; qc.queueCount=1; qc.pQueuePriorities=&priority;
    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,&f13};
    VkPhysicalDeviceFeatures2 available{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,&f12}; vkGetPhysicalDeviceFeatures2(physical,&available);
    indirect=f12.drawIndirectCount && available.features.multiDrawIndirect;
    f12={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,&f13}; f12.drawIndirectCount=indirect;
    f13={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES}; f13.dynamicRendering=f13.synchronization2=VK_TRUE;
    VkPhysicalDeviceFeatures core{}; core.multiDrawIndirect=indirect;
    std::vector<const char*> devExtensions;
    if(surface)devExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    uint32_t extCount=0;check(vkEnumerateDeviceExtensionProperties(physical,nullptr,&extCount,nullptr));
    std::vector<VkExtensionProperties> ext(extCount);check(vkEnumerateDeviceExtensionProperties(physical,nullptr,&extCount,ext.data()));
    const char* required[]={VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,VK_KHR_RAY_QUERY_EXTENSION_NAME,VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME};
    rayCapable=true;
    for(auto name:required)rayCapable &= std::any_of(ext.begin(),ext.end(),[&](auto& e){return strcmp(e.extensionName,name)==0;});
    VkPhysicalDeviceRayQueryFeaturesKHR rq{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR as{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,&rq};
    VkPhysicalDeviceBufferDeviceAddressFeatures address{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES,&as};
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,&address};vkGetPhysicalDeviceFeatures2(physical,&features);
    rayCapable=rayCapable&&rq.rayQuery&&as.accelerationStructure&&address.bufferDeviceAddress;
    if(rayCapable){devExtensions.insert(devExtensions.end(),std::begin(required),std::end(required));
        as={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,&rq};as.accelerationStructure=VK_TRUE;
        f13.pNext=&as;f12.bufferDeviceAddress=VK_TRUE;}
    VkDeviceCreateInfo dc{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,&f12}; dc.pEnabledFeatures=&core; dc.queueCreateInfoCount=1; dc.pQueueCreateInfos=&qc;
    dc.enabledExtensionCount=uint32_t(devExtensions.size());dc.ppEnabledExtensionNames=devExtensions.data();
    check(vkCreateDevice(physical,&dc,nullptr,&device)); volkLoadDevice(device); life.add([this]{vkDestroyDevice(device,nullptr);});
    vkGetDeviceQueue(device,family,0,&queue);
    VmaVulkanFunctions loader{}; loader.vkGetInstanceProcAddr=vkGetInstanceProcAddr; loader.vkGetDeviceProcAddr=vkGetDeviceProcAddr;
    VmaAllocatorCreateInfo ac{}; if(rayCapable)ac.flags=VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT; ac.physicalDevice=physical; ac.device=device; ac.instance=instance; ac.vulkanApiVersion=VK_API_VERSION_1_3; ac.pVulkanFunctions=&loader;
    check(vmaCreateAllocator(&ac,&allocator)); life.add([this]{vmaDestroyAllocator(allocator);});
    VkCommandPoolCreateInfo pc{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; pc.queueFamilyIndex=family; pc.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    check(vkCreateCommandPool(device,&pc,nullptr,&pool)); life.add([this]{vkDestroyCommandPool(device,pool,nullptr);});
    for(auto& f:frames){
        VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; alloc.commandPool=pool; alloc.commandBufferCount=1; check(vkAllocateCommandBuffers(device,&alloc,&f.cmd));
        VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fc.flags=VK_FENCE_CREATE_SIGNALED_BIT;
        check(vkCreateFence(device,&fc,nullptr,&f.fence)); life.add([this,&f]{vkDestroyFence(device,f.fence,nullptr);});
        VkSemaphoreCreateInfo sc{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        check(vkCreateSemaphore(device,&sc,nullptr,&f.acquired)); life.add([this,&f]{vkDestroySemaphore(device,f.acquired,nullptr);});
        VkQueryPoolCreateInfo q{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO}; q.queryType=VK_QUERY_TYPE_TIMESTAMP; q.queryCount=8;
        check(vkCreateQueryPool(device,&q,nullptr,&f.queries)); life.add([this,&f]{vkDestroyQueryPool(device,f.queries,nullptr);});
    }
    resize();
    VkPipelineLayoutCreateInfo lc{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    check(vkCreatePipelineLayout(device,&lc,nullptr,&triangleLayout)); life.add([this]{vkDestroyPipelineLayout(device,triangleLayout,nullptr);});
    VkPipelineVertexInputStateCreateInfo inputs{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    triangle=graphics(life,triangleLayout,"triangle.vert","triangle.frag",inputs,false);
    if(window) startGui();
    std::cout<<"GPU: "<<properties.deviceName<<" | "<<size.width<<"x"<<size.height<<"\n";
}
Buffer Gpu::buffer(Lifetime& owner,size_t bytes,VkBufferUsageFlags usage,bool host){
    Buffer result; VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; ci.size=bytes; ci.usage=usage;
    VmaAllocationCreateInfo ac{}; ac.usage=VMA_MEMORY_USAGE_AUTO;
    if(host) ac.flags=VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT|VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo ai{}; check(vmaCreateBuffer(allocator,&ci,&ac,&result.handle,&result.allocation,&ai)); result.mapped=ai.pMappedData;
    owner.add([this,result]{vmaDestroyBuffer(allocator,result.handle,result.allocation);}); return result;
}
Image Gpu::image(Lifetime& owner,VkFormat fmt,VkImageUsageFlags usage,VkImageAspectFlags aspect,VkExtent2D extent){
    Image r; VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; ci.imageType=VK_IMAGE_TYPE_2D; ci.format=fmt;
    if(!extent.width)extent=size; ci.extent={extent.width,extent.height,1}; ci.mipLevels=ci.arrayLayers=1; ci.samples=VK_SAMPLE_COUNT_1_BIT; ci.usage=usage;
    VmaAllocationCreateInfo ac{}; ac.usage=VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    check(vmaCreateImage(allocator,&ci,&ac,&r.handle,&r.allocation,nullptr));
    owner.add([this,r]{vmaDestroyImage(allocator,r.handle,r.allocation);});
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; vi.image=r.handle; vi.viewType=VK_IMAGE_VIEW_TYPE_2D; vi.format=fmt;
    vi.subresourceRange={aspect,0,1,0,1}; check(vkCreateImageView(device,&vi,nullptr,&r.view));
    owner.add([this,r]{vkDestroyImageView(device,r.view,nullptr);}); return r;
}
void Gpu::resize(){
    check(vkDeviceWaitIdle(device)); targets.clear(); presented.clear(); images.clear();
    if(window){
        VkSurfaceCapabilitiesKHR caps; check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical,surface,&caps)); size=caps.currentExtent;
        uint32_t n; check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical,surface,&n,nullptr)); std::vector<VkSurfaceFormatKHR> formats(n);
        check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical,surface,&n,formats.data())); auto fmt=formats.front();
        for(auto f:formats) if(f.format==VK_FORMAT_B8G8R8A8_UNORM) fmt=f;
        VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR}; ci.surface=surface;
        ci.minImageCount=std::max(2u,caps.minImageCount+1); if(caps.maxImageCount) ci.minImageCount=std::min(ci.minImageCount,caps.maxImageCount);
        ci.imageExtent=size; ci.imageFormat=fmt.format; ci.imageColorSpace=fmt.colorSpace; ci.imageArrayLayers=1;
        ci.imageUsage=VK_IMAGE_USAGE_TRANSFER_DST_BIT; ci.preTransform=caps.currentTransform; ci.compositeAlpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        ci.presentMode=VK_PRESENT_MODE_FIFO_KHR; ci.clipped=VK_TRUE;
        if(!vsync){ check(vkGetPhysicalDeviceSurfacePresentModesKHR(physical,surface,&n,nullptr)); std::vector<VkPresentModeKHR> modes(n);
            check(vkGetPhysicalDeviceSurfacePresentModesKHR(physical,surface,&n,modes.data()));
            for(auto m:modes) if(m==VK_PRESENT_MODE_MAILBOX_KHR || m==VK_PRESENT_MODE_IMMEDIATE_KHR) ci.presentMode=m;
        }
        check(vkCreateSwapchainKHR(device,&ci,nullptr,&swap)); targets.add([this]{vkDestroySwapchainKHR(device,swap,nullptr);});
        check(vkGetSwapchainImagesKHR(device,swap,&n,nullptr)); images.resize(n); check(vkGetSwapchainImagesKHR(device,swap,&n,images.data()));
        presented.resize(n); VkSemaphoreCreateInfo sc{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        for(auto& s:presented){check(vkCreateSemaphore(device,&sc,nullptr,&s)); targets.add([this,s]{vkDestroySemaphore(device,s,nullptr);});}
    }
    for(auto& f:frames){
        f.color=image(targets,format,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,VK_IMAGE_ASPECT_COLOR_BIT);
        f.depth=image(targets,VK_FORMAT_D32_SFLOAT,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,VK_IMAGE_ASPECT_DEPTH_BIT);
    }
}
void Gpu::startGui(){
    ImGui::CreateContext(); life.add([]{ImGui::DestroyContext();}); ImGui::GetIO().IniFilename=nullptr;
    ImGui_ImplGlfw_InitForVulkan(window,true); life.add([]{ImGui_ImplGlfw_Shutdown();});
    ImGui_ImplVulkan_InitInfo i{}; i.ApiVersion=VK_API_VERSION_1_3; i.Instance=instance; i.PhysicalDevice=physical; i.Device=device;
    i.QueueFamily=family; i.Queue=queue; i.DescriptorPoolSize=32; i.MinImageCount=i.ImageCount=2; i.UseDynamicRendering=true;
    i.PipelineRenderingCreateInfo={VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO}; i.PipelineRenderingCreateInfo.colorAttachmentCount=1;
    i.PipelineRenderingCreateInfo.pColorAttachmentFormats=&format;
    if(!ImGui_ImplVulkan_Init(&i)) throw std::runtime_error("ImGui initialization failed");
    life.add([]{ImGui_ImplVulkan_Shutdown();}); gui=true;
}
VkShaderModule Gpu::module(Lifetime& owner,const char* file){
    std::ifstream in(shaders/(std::string(file)+".spv"),std::ios::binary|std::ios::ate);
    if(!in) throw std::runtime_error(std::string("Missing shader ")+file);
    size_t bytes=size_t(in.tellg()); std::vector<uint32_t> data(bytes/4); in.seekg(0); in.read(reinterpret_cast<char*>(data.data()),bytes);
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; ci.codeSize=bytes; ci.pCode=data.data();
    VkShaderModule m; check(vkCreateShaderModule(device,&ci,nullptr,&m)); owner.add([this,m]{vkDestroyShaderModule(device,m,nullptr);}); return m;
}
VkPipeline Gpu::graphics(Lifetime& owner,VkPipelineLayout layout,const char* vs,const char* fs,VkPipelineVertexInputStateCreateInfo inputs,bool depth,int pass){
    Lifetime modules; VkPipelineShaderStageCreateInfo stages[2]{};
    for(int i=0;i<2;i++){stages[i].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stages[i].stage=i?VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_VERTEX_BIT;
        stages[i].module=module(modules,i?fs:vs); stages[i].pName="main";}
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; vp.viewportCount=vp.scissorCount=1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO}; rs.lineWidth=1;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState ba{}; ba.colorWriteMask=15;
    VkPipelineColorBlendStateCreateInfo bs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; bs.attachmentCount=1; bs.pAttachments=&ba;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO}; ds.depthTestEnable=ds.depthWriteEnable=depth; ds.depthCompareOp=VK_COMPARE_OP_LESS;
    VkDynamicState states[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO}; dyn.dynamicStateCount=2; dyn.pDynamicStates=states;
    VkPipelineRenderingCreateInfo r{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO}; r.colorAttachmentCount=1; r.pColorAttachmentFormats=&format;
    VkFormat mrt[]={VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_R8G8B8A8_UNORM};
    VkPipelineColorBlendAttachmentState blend[]={ba,ba};
    if(pass==1){r.colorAttachmentCount=bs.attachmentCount=2;r.pColorAttachmentFormats=mrt;bs.pAttachments=blend;}
    if(pass==2){r.colorAttachmentCount=bs.attachmentCount=0;}
    VkFormat hdr=VK_FORMAT_R16G16B16A16_SFLOAT;
    if(pass==3)r.pColorAttachmentFormats=&hdr;
    if(depth) r.depthAttachmentFormat=VK_FORMAT_D32_SFLOAT;
    VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,&r}; ci.stageCount=2; ci.pStages=stages; ci.pVertexInputState=&inputs;
    ci.pInputAssemblyState=&ia; ci.pViewportState=&vp; ci.pRasterizationState=&rs; ci.pMultisampleState=&ms; ci.pColorBlendState=&bs;
    ci.pDynamicState=&dyn; ci.pDepthStencilState=&ds; ci.layout=layout;
    VkPipeline p; check(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&ci,nullptr,&p)); owner.add([this,p]{vkDestroyPipeline(device,p,nullptr);}); return p;
}
void transition(VkCommandBuffer cmd,VkImage img,VkImageAspectFlags aspect,VkImageLayout before,VkImageLayout after,
                VkPipelineStageFlags2 src,VkAccessFlags2 written,VkPipelineStageFlags2 dst,VkAccessFlags2 needed){
    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2}; b.srcStageMask=src; b.srcAccessMask=written; b.dstStageMask=dst; b.dstAccessMask=needed;
    b.oldLayout=before; b.newLayout=after; b.image=img; b.subresourceRange={aspect,0,1,0,1}; b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO}; di.imageMemoryBarrierCount=1; di.pImageMemoryBarriers=&b; vkCmdPipelineBarrier2(cmd,&di);
}
void Gpu::render(Frame& f,bool depth,bool load){
    VkRect2D rect{{0,0},size}; VkViewport vp{0,0,float(size.width),float(size.height),0,1};
    vkCmdSetViewport(f.cmd,0,1,&vp); vkCmdSetScissor(f.cmd,0,1,&rect);
    VkRenderingAttachmentInfo col{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO}; col.imageView=f.color.view; col.imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    col.loadOp=load?VK_ATTACHMENT_LOAD_OP_LOAD:VK_ATTACHMENT_LOAD_OP_CLEAR; col.storeOp=VK_ATTACHMENT_STORE_OP_STORE; col.clearValue.color={{.035f,.071f,.12f,1}};
    VkRenderingAttachmentInfo z{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO}; z.imageView=f.depth.view; z.imageLayout=VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    z.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR; z.storeOp=VK_ATTACHMENT_STORE_OP_STORE; z.clearValue.depthStencil={1,0};
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO}; ri.renderArea=rect; ri.layerCount=1; ri.colorAttachmentCount=1; ri.pColorAttachments=&col; ri.pDepthAttachment=depth?&z:nullptr;
    vkCmdBeginRendering(f.cmd,&ri);
}
void Gpu::trianglePass(Frame& f){
    render(f,false); vkCmdBindPipeline(f.cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,triangle); vkCmdDraw(f.cmd,3,1,0,0); vkCmdEndRendering(f.cmd);
}
bool Gpu::draw(const std::function<void(Frame&)>& record,const std::filesystem::path& capture,double* gpuMs){
    auto& f=frames[sequence%2]; check(vkWaitForFences(device,1,&f.fence,VK_TRUE,UINT64_MAX)); uint32_t idx=0;
    if(window){int w,h; glfwGetFramebufferSize(window,&w,&h); if(w==0||h==0){glfwWaitEventsTimeout(.1);return false;}
        if(size.width!=uint32_t(w)||size.height!=uint32_t(h)) resize();
        auto r=vkAcquireNextImageKHR(device,swap,UINT64_MAX,f.acquired,VK_NULL_HANDLE,&idx);
        if(r==VK_ERROR_OUT_OF_DATE_KHR){resize();return false;} if(r!=VK_SUBOPTIMAL_KHR) check(r);
    }
    Lifetime scratch; Buffer read{};
    if(!capture.empty()) read=buffer(scratch,size_t(size.width)*size.height*4,VK_BUFFER_USAGE_TRANSFER_DST_BIT,true);
    check(vkResetCommandBuffer(f.cmd,0)); VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; check(vkBeginCommandBuffer(f.cmd,&bi));
    vkCmdResetQueryPool(f.cmd,f.queries,0,8); vkCmdWriteTimestamp2(f.cmd,VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,f.queries,0);
    constexpr auto colorStage=VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    transition(f.cmd,f.color.handle,1,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,0,0,colorStage,VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    transition(f.cmd,f.depth.handle,2,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,0,0,VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    record(f);
    if(gui && ImGui::GetDrawData()){
        transition(f.cmd,f.color.handle,1,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,colorStage,VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,colorStage,VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        render(f,false,true); ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(),f.cmd); vkCmdEndRendering(f.cmd);
    }
    vkCmdWriteTimestamp2(f.cmd,VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,f.queries,1);
    transition(f.cmd,f.color.handle,1,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,colorStage,VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,VK_PIPELINE_STAGE_2_TRANSFER_BIT,VK_ACCESS_2_TRANSFER_READ_BIT);
    if(read.handle){ VkBufferImageCopy copy{}; copy.imageSubresource={1,0,0,1}; copy.imageExtent={size.width,size.height,1};
        vkCmdCopyImageToBuffer(f.cmd,f.color.handle,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,read.handle,1,&copy);}
    if(window){
        transition(f.cmd,images[idx],1,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,0,VK_PIPELINE_STAGE_2_TRANSFER_BIT,VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkImageBlit blit{}; blit.srcSubresource=blit.dstSubresource={1,0,0,1}; blit.srcOffsets[1]=blit.dstOffsets[1]={int(size.width),int(size.height),1};
        vkCmdBlitImage(f.cmd,f.color.handle,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,images[idx],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&blit,VK_FILTER_NEAREST);
        transition(f.cmd,images[idx],1,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,VK_PIPELINE_STAGE_2_TRANSFER_BIT,VK_ACCESS_2_TRANSFER_WRITE_BIT,0,0);
    }
    check(vkEndCommandBuffer(f.cmd)); VkCommandBufferSubmitInfo cb{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO}; cb.commandBuffer=f.cmd;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2}; submit.commandBufferInfoCount=1; submit.pCommandBufferInfos=&cb;
    VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO},signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    if(window){wait.semaphore=f.acquired; wait.stageMask=VK_PIPELINE_STAGE_2_TRANSFER_BIT; signal.semaphore=presented[idx]; signal.stageMask=VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        submit.waitSemaphoreInfoCount=submit.signalSemaphoreInfoCount=1; submit.pWaitSemaphoreInfos=&wait; submit.pSignalSemaphoreInfos=&signal;}
    check(vkResetFences(device,1,&f.fence)); check(vkQueueSubmit2(queue,1,&submit,f.fence));
    if(window){VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR}; pi.waitSemaphoreCount=1; pi.pWaitSemaphores=&presented[idx]; pi.swapchainCount=1; pi.pSwapchains=&swap; pi.pImageIndices=&idx;
        auto r=vkQueuePresentKHR(queue,&pi); if(r!=VK_SUBOPTIMAL_KHR && r!=VK_ERROR_OUT_OF_DATE_KHR) check(r);}
    if(gpuMs || read.handle) check(vkWaitForFences(device,1,&f.fence,VK_TRUE,UINT64_MAX));
    if(gpuMs){uint64_t ticks[2]; check(vkGetQueryPoolResults(device,f.queries,0,2,sizeof(ticks),ticks,8,VK_QUERY_RESULT_64_BIT));
        *gpuMs=double(ticks[1]-ticks[0])*properties.limits.timestampPeriod*1e-6;}
    if(read.handle){check(vmaInvalidateAllocation(allocator,read.allocation,0,VK_WHOLE_SIZE));
        if(capture.has_parent_path()) std::filesystem::create_directories(capture.parent_path());
        std::ofstream out(capture,std::ios::binary);
        int ok=stbi_write_png_to_func([](void* p,void* data,int n){static_cast<std::ostream*>(p)->write(static_cast<char*>(data),n);},static_cast<std::ostream*>(&out),size.width,size.height,4,read.mapped,size.width*4);
        if(!ok||!out) throw std::runtime_error("PNG write failed");}
    ++sequence; return true;
}


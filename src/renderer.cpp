#include "renderer.hpp"
#include "gpu.hpp"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <windows.h>
#include <iostream>
int launch(int argc,char** argv){
    try {
        bool hidden=false,validate=false; std::filesystem::path output; int limit=0;
        for(int i=1;i<argc;i++){std::string a=argv[i];
            if(a=="--capture" && i+1<argc){hidden=true;output=argv[++i];limit=1;}
            else if(a=="--validation") validate=true;
            else if(a=="--frames" && i+1<argc) limit=std::stoi(argv[++i]);
        }
        {
            Gpu gpu; wchar_t path[32768]; GetModuleFileNameW(nullptr,path,32768); gpu.shaders=std::filesystem::path(path).parent_path()/"shaders";
            gpu.init(1600,900,!hidden,validate,true);
            while((!limit || gpu.sequence<uint32_t(limit)) && (!gpu.window || !glfwWindowShouldClose(gpu.window))){
                if(gpu.window){glfwPollEvents(); ImGui_ImplVulkan_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();
                    ImGui::Begin("Voxelith"); ImGui::Text("Vulkan 1.3 | %s",gpu.properties.deviceName); ImGui::End(); ImGui::Render();}
                gpu.draw([&](Frame& f){gpu.trianglePass(f);},output);
            }
        }
        if(validate) std::cout<<"Validation: "<<validationWarnings<<" warnings, "<<validationErrors<<" errors\n";
        return validationErrors || validationWarnings ? 2:0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<"\n"; return 1;}
}

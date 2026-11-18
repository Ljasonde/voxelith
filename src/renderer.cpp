#include "renderer.hpp"
#include "game.hpp"
#include <windows.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <numeric>

struct Options{
    bool triangle=false,validation=false,benchmark=false,culling=true;
    int width=1600,height=900,frames=0,shot=0,distance=12,seed=241128;
    float scale=1;std::filesystem::path capture,results,sequence;
};
Options parse(int argc,char** argv){
    Options o;
    for(int i=1;i<argc;i++){std::string a=argv[i];auto value=[&](){if(++i>=argc)throw std::runtime_error("Missing value for "+a);return std::string(argv[i]);};
        if(a=="--triangle")o.triangle=true;else if(a=="--validation")o.validation=true;
        else if(a=="--benchmark"){o.benchmark=true;o.results=value();}else if(a=="--capture")o.capture=value();
        else if(a=="--sequence")o.sequence=value();else if(a=="--frames")o.frames=std::stoi(value());
        else if(a=="--width")o.width=std::stoi(value());else if(a=="--height")o.height=std::stoi(value());
        else if(a=="--shot")o.shot=std::stoi(value());else if(a=="--distance")o.distance=std::stoi(value());
        else if(a=="--culling")o.culling=std::stoi(value())!=0;else if(a=="--seed")o.seed=std::stoi(value());
        else if(a=="--scale")o.scale=std::stof(value());else throw std::runtime_error("Unknown option "+a);
    }
    if(o.width<64||o.width>8192||o.height<64||o.height>8192||o.frames<0||o.shot<0||o.distance<1||o.distance>24||!(o.scale>=.7f&&o.scale<=1))throw std::runtime_error("Invalid dimensions, scale, shot, frame count or distance");
    return o;
}
struct Sample{double cpu,gpu,total;uint32_t visible,triangles;};
static double percentile(std::vector<double> a,double p){std::sort(a.begin(),a.end());return a[size_t(std::ceil(p*a.size()))-1];}
static void bench(Gpu& g,Scene& s,const Options& o){
    using Clock=std::chrono::steady_clock;auto ms=[](auto d){return std::chrono::duration<double,std::milli>(d).count();};
    std::vector<Sample> rows;std::filesystem::create_directories(o.results);
    for(int i=-120;i<600;i++){
        auto start=Clock::now();float t=float(std::max(0,i))/60;auto camera=tour(t);double gpu=0,cpu=0;
        g.draw([&](Frame& f){s.draw(f,camera,t);cpu=ms(Clock::now()-start);},{},&gpu);
        if(i>=0)rows.push_back({cpu,gpu,ms(Clock::now()-start),s.visible,s.visibleTriangles});
    }
    std::ofstream csv(o.results/"frames.csv");csv<<"timestamp_ms,scene,width,height,scale,rt,culling,view_distance,frame,cpu_record_ms,gpu_ms,total_ms,visible,triangles\n";
    std::vector<double> times,gpus;double sum=0;int i=0;
    for(auto& r:rows){sum+=r.total;times.push_back(r.total);gpus.push_back(r.gpu);csv<<sum<<",spire,"<<o.width<<","<<o.height<<","<<o.scale<<",0,"<<o.culling<<","<<o.distance<<","<<i++<<","<<r.cpu<<","<<r.gpu<<","<<r.total<<","<<r.visible<<","<<r.triangles<<"\n";}
    std::ofstream json(o.results/"summary.json");json<<std::setprecision(8)<<"{\n  \"gpu\": \""<<g.properties.deviceName<<"\", \"seed\": "<<o.seed<<", \"warmup\": 120, \"frames\": 600, \"width\": "<<o.width<<", \"height\": "<<o.height<<", \"scale\": "<<o.scale<<", \"rt\": false, \"culling\": "<<(o.culling?"true":"false")<<", \"distance\": "<<o.distance<<",\n  \"avg_fps\": "<<600000/sum<<", \"p1_ms\": "<<percentile(times,.01)<<", \"p99_ms\": "<<percentile(times,.99)<<", \"avg_ms\": "<<sum/600<<", \"avg_gpu_ms\": "<<std::accumulate(gpus.begin(),gpus.end(),0.)/600<<", \"chunks\": "<<s.chunks.size()<<", \"triangles\": "<<s.triangles<<"\n}\n";
    if(!csv||!json)throw std::runtime_error("Benchmark write failed");std::cout<<600000/sum<<" fps, p99 "<<percentile(times,.99)<<" ms\n";
}
int launch(int argc,char** argv){
    try {
        auto o=parse(argc,argv);bool window=!o.benchmark&&o.capture.empty()&&o.sequence.empty();
        {
            Gpu g;wchar_t path[32768];GetModuleFileNameW(nullptr,path,32768);auto dir=std::filesystem::path(path).parent_path();g.shaders=dir/"shaders";
            g.init(o.width,o.height,window,o.validation,!o.benchmark);
            if(o.triangle){do{if(window){glfwPollEvents();ImGui_ImplVulkan_NewFrame();ImGui_ImplGlfw_NewFrame();ImGui::NewFrame();ImGui::Begin("Voxelith");ImGui::TextUnformatted(g.properties.deviceName);ImGui::End();ImGui::Render();}
                g.draw([&](Frame& f){g.trianglePass(f);},o.capture);}while(window&&!glfwWindowShouldClose(g.window)&&(!o.frames||int(g.sequence)<o.frames));}
            else{World w(o.seed);Scene s(g,w);s.culling=o.culling;s.distance=o.distance;
                if(o.benchmark)bench(g,s,o);else if(window)play(g,w,s,dir,o.frames);
                else{int count=o.sequence.empty()?1:std::max(1,o.frames);
                    for(int i=0;i<count;i++){float t=i/60.f;int shot=o.shot+(o.sequence.empty()?0:int(t/12)%5);auto cam=tour(t,shot);auto dest=o.capture;
                        if(!o.sequence.empty()){std::ostringstream name;name<<"frame"<<std::setw(5)<<std::setfill('0')<<i<<".png";dest=o.sequence/name.str();}
                        g.draw([&](Frame& f){s.draw(f,cam,t);},dest);}
                }}
        }
        if(o.validation)std::cout<<"Validation: "<<validationWarnings<<" warnings, "<<validationErrors<<" errors\n";
        return validationErrors||validationWarnings?2:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}

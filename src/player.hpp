#pragma once
#include "world.hpp"
#include <filesystem>
#include <string>
struct Player {
    glm::vec3 feet{},velocity{};float yaw=-2.35f,pitch=0,elapsed=0;int mask=0,slot=0;
    bool flying=false,grounded=false,won=false;
    glm::vec3 forward()const;
    glm::vec3 eye()const{return feet+glm::vec3(0,1.62f,0);}
    void step(World& w,glm::vec3 input,float dt,bool jump,bool sprint);
    bool use(World& w,bool place,glm::ivec3& dirty,std::string& message);
    void portal(World& w);
    void save(const World& w,const std::filesystem::path& path)const;
    bool load(World& w,const std::filesystem::path& path);
};
inline constexpr Block Hotbar[]={Grass,Stone,Wood,Planks,Brick,Trim,Crystal,Petals,Water};
inline constexpr const char* Names[]={"Grass","Stone","Log","Planks","Brick","Trim","Crystal","Blossom","Water"};


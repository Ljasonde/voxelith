#pragma once
#include <glm/glm.hpp>
#include <array>
#include <vector>
#include <cstdint>
#include <map>
#include <functional>
enum Block : uint8_t {Air,Grass,Soil,Stone,Wood,Leaves,Sand,Snow,Petals,Basalt,Lava,Water,Brick,Trim,Crystal,Amber,Moss,Planks,CoreBlue,CoreGreen,CoreGold,Portal};
constexpr int Side=416, Height=128, Chunk=32, Across=Side/Chunk, Chunks=Across*Across*(Height/Chunk);
struct Vertex {glm::vec3 pos; uint32_t face;};
struct Mesh {std::vector<Vertex> vertices; std::vector<uint32_t> indices;};
struct Island {int x,z,r,level,biome;};
struct Hit {bool found{};glm::ivec3 block{},previous{};};
struct World {
    int seed; std::vector<uint8_t> cells;
    std::array<bool,Chunks> occupied{};
    std::array<glm::ivec3,3> cores{};
    glm::ivec3 gateway{};
    std::map<int,int> edits;
    explicit World(int seed=241128);
    static int key(glm::ivec3 p){return (p.z*Height+p.y)*Side+p.x;}
    static glm::ivec3 unpack(int n){return {n%Side,(n/Side)%Height,n/(Height*Side)};}
    static bool inside(glm::ivec3 p){return p.x>=0&&p.x<Side&&p.y>=0&&p.y<Height&&p.z>=0&&p.z<Side;}
    Block get(glm::ivec3 p)const{return inside(p)?Block(cells[key(p)]):Air;}
    void set(glm::ivec3 p,Block b);
    void edit(glm::ivec3 p,Block b){if(inside(p)){set(p,b);edits[key(p)]=b;}}
    bool solid(glm::ivec3 p)const{auto b=get(p);return b!=Air&&b!=Water&&b!=Portal;}
    Mesh mesh(int id)const;
    static glm::ivec3 origin(int id){return Chunk*glm::ivec3(id%Across,id/(Across*Across),(id/Across)%Across);}
    static int chunkId(glm::ivec3 p){return (p.y/Chunk*Across+p.z/Chunk)*Across+p.x/Chunk;}
    Hit pick(glm::vec3 pos,glm::vec3 direction,float reach=6)const;
    bool overlaps(glm::vec3 feet)const;
    glm::vec3 spawn()const;
};
void parallelFor(int count,const std::function<void(int)>& work);


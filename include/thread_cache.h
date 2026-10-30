#pragma once

#include <cstddef>
namespace hui {
class ThreadCache{
#define NODE_NUMS 32
struct node{
    node(){}
    node(node* n,void* ad):next(n),memory_ptr(ad){}
    node* next;
    void* memory_ptr;
};

public:
    ThreadCache(){
        InitCache();
    }

    ~ThreadCache(){}

    void* AllocateInCache(size_t t_size);
    int DeleteInCache(size_t t_size,void* memory_ptr);

private:
    void InitCache();
    void  ReFillCache(size_t t_size);

private:
    node* free_node_array_[64];
    size_t free_node_count_[64] = {0};
    size_t all_node[64] = {0};

};
}
#include "../include/thread_cache.h"
#include "../include/memory_pool.h"
#include <cstddef>

namespace hui{
void* ThreadCache::AllocateInCache(size_t t_size){
    int key = (t_size - 1) / 8;
    if(all_node[key] >= NODE_NUMS) return nullptr;
    if(free_node_count_[key] == 0 && free_node_array_[key]->next == nullptr){
        ReFillCache(t_size);
    }

    //将地址分配出去
    void* address = free_node_array_[key]->next->memory_ptr;
    free_node_array_[key]->next = free_node_array_[key]->next->next;
    free_node_count_[key] -= 1;
    all_node[key] -= 1;
    return address;
}

int ThreadCache::DeleteInCache(size_t t_size,void* memory_ptr){
    int key = (t_size - 1) / 8;
    if(free_node_count_[key] == 0 && free_node_array_[key]->next == nullptr)        return -1;//异常处理

    //将地址回收进来
    if(all_node[key] >= NODE_NUMS)     
        return 0;
    else{
        node* node_ptr = new node(free_node_array_[key]->next,memory_ptr);
        free_node_array_[key]->next = node_ptr;
        free_node_count_[key] += 1;
        all_node[key] += 1;
        return 1;
    }
}

void ThreadCache::InitCache(){
    for(int i = 0; i < 64;i++){
        free_node_array_[i] = new node(nullptr,nullptr);
    }
}

void ThreadCache::ReFillCache(size_t t_size){
    int key = (t_size - 1) / 8;
    for(int i = 0; i < 8; i++){
        if(all_node[key] > NODE_NUMS) 
            break;

        void* address = HashTable::UseMemory(t_size);
        if(free_node_array_[key]->next == nullptr){
            free_node_array_[key]->next = new node(nullptr,address);
        }
        else{
            auto tmp = free_node_array_[key]->next;
            free_node_array_[key]->next = new node(nullptr,address);
            free_node_array_[key]->next->next = tmp;
        }

        free_node_count_[key] += 1;
        all_node[key] += 1;
    }
}
}


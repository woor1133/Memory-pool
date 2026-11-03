#include "../include/thread_cache.h"
#include "../include/memory_pool.h"
#include <cstddef>

namespace hui{
void* ThreadCache::AllocateInCache(size_t t_size){
    int key = (t_size - 1) / 8;
    //没有空闲节点，需要分批
    if(free_node_count_[key] == 0 && free_node_array_[key]->next == nullptr){
        ReFillCache(t_size);
    }

    //将地址分配出去
    void* address = reinterpret_cast<void*>( free_node_array_[key]->next);
    free_node_array_[key]->next = free_node_array_[key]->next->next;
    free_node_count_[key] -= 1;
    //未实现:回收node内存

    return address;
}

int ThreadCache::DeleteInCache(size_t t_size,void* memory_ptr){
    int key = (t_size - 1) / 8;

    //将地址回收进来
    if(free_node_count_[key] >= NODE_NUMS)     //空闲节点达到上限，不再回收
        return 0;
    else{
        node* node_ptr = static_cast<node*>(memory_ptr);           
        node_ptr->next = free_node_array_[key]->next;
        free_node_array_[key]->next = node_ptr;
        free_node_count_[key] += 1;
        return 1;
    }
}

void ThreadCache::InitCache(){
    for(int i = 0; i < 64;i++){
        free_node_array_[i] = new node(nullptr);
    }
}

void ThreadCache::ReFillCache(size_t t_size){
    int key = (t_size - 1) / 8;
    for(int i = 0; i < 8; i++){
        if(free_node_count_[key] >= NODE_NUMS)       //空闲节点达到上限
            break;

        void* address = HashTable::UseMemory(t_size);
        
        //头插法
        auto tmp = free_node_array_[key]->next;
        free_node_array_[key]->next = static_cast<node*>(address);
        free_node_array_[key]->next->next = tmp;
        free_node_count_[key] += 1;
    }
}

//析构函数未实现
void ThreadCache::Destruct(){
    for(int i = 0; i < 64;i++){
        auto ptr = free_node_array_[i];
        while(ptr->next != nullptr){
            auto tmp = ptr->next;
            ptr->next = ptr->next->next;
            HashTable::FreeMemory((i+1)*8, reinterpret_cast<void*>(tmp));
        }
        delete free_node_array_[i];
    }
}
}


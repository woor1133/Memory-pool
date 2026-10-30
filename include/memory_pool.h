#pragma once
#include <cstdint>
#include <cstddef>
#include <mutex>
#include <utility>
#include <thread>
#include "thread_cache.h"
#define BLOCK_SIZE 4096
#define POOLS_NUM 64
namespace hui{

class MemoryPool{
private:
    struct slot{
        slot* next;
    };
public:
    MemoryPool(){
        slot_size_ = 0;
        free_slot_ptr_ = nullptr;
        cur_slot_ptr_ = nullptr;
        first_slot_ptr_ = nullptr;
        last_slot_ptr_ = nullptr;
    }

    MemoryPool(size_t slot_size)  //构造函数
    {
        slot_size_ = slot_size;
        free_slot_ptr_ = nullptr;
        cur_slot_ptr_ = nullptr;
        first_slot_ptr_ = nullptr;
        last_slot_ptr_ = nullptr;
    }
    ~MemoryPool(){}
    void SetSlotSize(size_t slot_size){this->slot_size_ = slot_size;}
    void* Allocate();
    void Deallocate(void* memory_ptr);
    
private:

    void AllocateBlock();
    void* MemoryAlign(std::uintptr_t address,size_t align_value);
private:
    std::size_t         slot_size_;
    std::size_t         block_size_ = BLOCK_SIZE;
    slot*               free_slot_ptr_;
    slot*               cur_slot_ptr_;
    slot*               first_slot_ptr_;
    slot*               last_slot_ptr_;

    std::mutex mutex_for_free_list_;
    std::mutex mutex_for_block_;

};



class HashTable{
public:
    static void InitMemoryPools(){
        
        for(int i =0 ; i < POOLS_NUM ; i++){
            memory_pools_[i].SetSlotSize((i+1)*8);
        }
    }


    static void* UseMemory(size_t T_size){
        int key = (T_size - 1) / 8;
        return memory_pools_[key].Allocate();
    }

    static void FreeMemory(size_t T_size,void* memory_ptr){
        int key = (T_size - 1) / 8;
        memory_pools_[key].Deallocate(memory_ptr);
    }
private:
    static MemoryPool memory_pools_[POOLS_NUM];
};

extern thread_local ThreadCache cache;

template <typename T,typename... Args>
T* NewElement(Args&&... args){
    size_t T_size = sizeof(T);
    //大内存直接分配
    //!!!注意：在 x86-64 Linux 上，普通 new 最终通常依赖 malloc，一般只保证 16 字节对齐。它有时可能恰好返回 128 字节对齐的地址，但这不是接口保证
    if(T_size > 512) return new T(std::forward<Args>(args)...);

    else if (void* addr = cache.AllocateInCache(T_size); addr != nullptr) {
        return new(reinterpret_cast<T*>(addr)) T(std::forward<Args>(args)...);
    }

    //分配区域并实例化对象(完美转发)
    else return  new (reinterpret_cast<T*>(HashTable::UseMemory(T_size))) T(std::forward<Args>(args)...);
}

template <typename T>
void DeleteElement(T* delete_element){
    //对象析构
    delete_element->~T();

    size_t T_size = sizeof(T);
    //大内存直接使用系统调用
    if(T_size > 512) operator delete(delete_element);

    else if(int res = cache.DeleteInCache(T_size, reinterpret_cast<void*>(delete_element));res == 1)
        return;

    //回收内存
    else HashTable::FreeMemory(T_size, reinterpret_cast<void*>(delete_element));
}

}
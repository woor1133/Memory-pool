#include"../include/memory_pool.h"
#include <cstddef>
#include <cstdint>


namespace hui{
MemoryPool HashTable::memory_pools_[POOLS_NUM] = {};

void* MemoryPool::Allocate(){
    //查找空闲块
    if(free_slot_ptr_ != nullptr){
        return reinterpret_cast<void*>(free_slot_ptr_);
        free_slot_ptr_ = free_slot_ptr_->next;
    }
    //尝试从当前slot开始分配
    else{
        if(cur_slot_ptr_ >= last_slot_ptr_ || last_slot_ptr_ == nullptr){
            AllocateBlock();
        }
        void* return_address = reinterpret_cast<void*>(cur_slot_ptr_);
        cur_slot_ptr_ = reinterpret_cast<slot*>(reinterpret_cast<char*>(cur_slot_ptr_) + slot_size_);
        return return_address;
    }
}

void MemoryPool::Deallocate(void* memory_ptr){
        //直接将内存区域回收到freelist中
        if(memory_ptr != nullptr){
            reinterpret_cast<slot*>(memory_ptr)->next = first_slot_ptr_;
            first_slot_ptr_ = reinterpret_cast<slot*>(memory_ptr);
    }
}

void MemoryPool::AllocateBlock(){
    //先分配空间
    void* start = operator new (BLOCK_SIZE);
    //内存对齐
    char* body = reinterpret_cast<char*>(start) + slot_size_;
    cur_slot_ptr_ = reinterpret_cast<slot*>(MemoryAlign(reinterpret_cast<uintptr_t>(body), slot_size_));

    //头插法修改内存池中的指向第一个
    reinterpret_cast<slot*>(start)->next = first_slot_ptr_;
    first_slot_ptr_ = reinterpret_cast<slot*>(start);

    //修改尾指针
    last_slot_ptr_ = reinterpret_cast<slot*>(reinterpret_cast<size_t>(start) + block_size_ - slot_size_ + 1);
}

void* MemoryPool::MemoryAlign(uintptr_t address,size_t align_value){
    return reinterpret_cast<void*>((address + align_value - 1) & ~(align_value - 1));
}

}

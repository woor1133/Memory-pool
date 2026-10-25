#pragma once
#include <cstddef>
#define BLOCK_SIZE 4096
namespace hui{

class MemoryPool{
public:
    MemoryPool(size_t slot_size)  //构造函数
    {
        slot_size_ = slot_size;
        free_slot_ptr_ = nullptr;
        cur_slot_ptr_ = nullptr;
        first_slot_ptr_ = nullptr;
    }
    ~MemoryPool(){}

    void allocate();
    void deallocate();
    
private:

private:
    struct slot{

    };
    std::size_t         slot_size_;
    std::size_t         block_size_ = BLOCK_SIZE;
    slot*               free_slot_ptr_;
    slot*               cur_slot_ptr_;
    slot*               first_slot_ptr_;

};

template <typename T>
T* NewElement(){}

template <typename T>
void DeleteElement(T* delete_element){}

}
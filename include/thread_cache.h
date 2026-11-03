#pragma once

#include <cstddef>
namespace hui {
class ThreadCache{
#define NODE_NUMS 32
struct node{
    node(){}
    node(node* n):next(n){}
    node* next;
    //void* memory_ptr;   //下一步删除内存指针，采用侵入式链表实现
};

public:
    ThreadCache(){
        InitCache();
    }

    ~ThreadCache() { Destruct(); }

    void* AllocateInCache(size_t t_size);
    int DeleteInCache(size_t t_size,void* memory_ptr);

private:
    void InitCache();
    void ReFillCache(size_t t_size);
    void Destruct();

private:
    node* free_node_array_[64];
    size_t free_node_count_[64] = {0};

    //删除all_node,语义不明确，结构冗杂
    //size_t all_node[64] = {0};

};
}
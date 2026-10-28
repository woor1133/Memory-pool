#include "../include/memory_pool.h"
#include <bits/types/clock_t.h>
#include <cstddef>
#include <ctime>
#include <iostream>
#include <thread>
#include <vector>
using namespace hui;

// 测试用例
class P1 
{
    int id_;
};

class P2 
{
    int id_[5];
};

class P3
{
    int id_[10];
};

class P4
{
    int id_[20];
};

// 单轮次申请释放次数 线程数 轮次
void BenchmarkMemoryPool(size_t ntimes, size_t nworks, size_t rounds)
{
	std::vector<std::thread> vthread(nworks); // 线程池
    vthread.reserve(nworks);

    // 所有线程启动前开始计时
    auto begin = std::chrono::steady_clock::now();
	for(size_t j = 0; j < nworks; ++j){
		vthread[j] = std::thread([&](){
			for(size_t k = 0; k < rounds; ++k){
				for(size_t t = 0; t < ntimes; ++t){
					P1* p1 = NewElement<P1>();
					DeleteElement(p1);
					P2* p2 = NewElement<P2>();
					DeleteElement(p2);
					P3* p3 = NewElement<P3>();
					DeleteElement(p3);
					P4* p4 = NewElement<P4>();
					DeleteElement(p4);
				}
			}

		});
	}

	for(auto& t : vthread) t.join();

	// 所有线程结束后停止计时
    auto end = std::chrono::steady_clock::now();

    double costtime = std::chrono::duration<double, std::milli>(end - begin).count();

    printf("%zu个线程并发执行%zu轮次，每轮次申请释放%zu次，总计花费：%.3f ms\n",
           nworks, rounds, ntimes, costtime);
}

void BenchmarkNew(size_t ntimes, size_t nworks, size_t rounds)
{
	std::vector<std::thread> vthread(nworks);
	vthread.reserve(nworks);

    // 所有线程启动前开始计时
    auto begin = std::chrono::steady_clock::now();
	for (size_t k = 0; k < nworks; ++k)
	{
		vthread[k] = std::thread([&]() {
			for (size_t j = 0; j < rounds; ++j)
			{
				for (size_t i = 0; i < ntimes; i++)
				{
                    P1* p1 = new P1;
                    //delete p1;
                    P2* p2 = new P2;
                    //delete p2;
                    P3* p3 = new P3;
                    //delete p3;
                    P4* p4 = new P4;
                    //delete p4;
				}
			}
		});
	}
	for (auto& t : vthread)
	{
		t.join();
	}
	// 所有线程结束后停止计时
    auto end = std::chrono::steady_clock::now();

    double total_costtime = std::chrono::duration<double, std::milli>(end - begin).count();
	printf("%lu个线程并发执行%lu轮次，每轮次malloc&free %lu次，总计花费：%lf ms\n", nworks, rounds, ntimes, total_costtime);
}

int main()
{
    HashTable::InitMemoryPools(); // 使用内存池接口前一定要先调用该函数
	BenchmarkMemoryPool(2000, 64, 10); // 测试内存池
	std::cout << "===========================================================================" << std::endl;
	std::cout << "===========================================================================" << std::endl;
	BenchmarkNew(2000, 64, 10); // 测试 new delete
	
	return 0;
}


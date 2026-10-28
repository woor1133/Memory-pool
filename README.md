# 个人内存池

这是一个用于学习和实践的内存池项目，使用 C++11 开发。

本项目的测试代码以及构建由AI给出

## 构建

测试程序需要 CMake 3.10 或更高版本，并依赖 C++11 和 pthread。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

构建后会生成两个测试程序：

* `build/correctness_test`：正确性与并发压力测试
* `build/performance_test`：可配置的性能基准

## 正确性测试

直接运行：

```bash
./build/correctness_test
```

或者通过 CTest 运行：

```bash
ctest --test-dir build --output-on-failure
```

测试覆盖以下接口行为：

* `NewElement` 的构造、参数转发和析构
* `DeleteElement` 的回收路径
* 大小类边界和大对象回退路径
* 大量同时存活对象之间的地址重叠
* 已释放内存的复用
* 对象对齐要求
* 构造异常传播
* 并发分配、校验和释放
* 跨线程释放

并发相关改动必须在提交前运行 TSan；同时建议运行 ASan 和 UBSan。示例：

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-asan --parallel
./build-asan/correctness_test
```

```bash
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"
cmake --build build-tsan --parallel
TSAN_OPTIONS=halt_on_error=1 ./build-tsan/correctness_test
```

## 性能测试

默认运行 `small`、`mixed` 和 `live` 三类场景，并比较内存池与等价的
`new/delete`。每次操作都会写入对象并释放，避免编译器删除基准代码。

```bash
./build/performance_test
```

常用参数：

```bash
./build/performance_test \
  --threads 1,2,4,8 \
  --iterations 100000 \
  --samples 5 \
  --scenario mixed \
  --csv performance.csv
```

查看全部参数：

```bash
./build/performance_test --help
```

性能结果使用中位数和 P95。建议比较相同机器、相同构建类型和相同场景下的
结果，不要混用 Debug、Release 或 sanitizer 构建。

只有 `correctness_test` 和 TSan 都通过后，性能结果才具有参考价值。

#include "memory_pool.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

struct TinyObject {
    std::uint32_t value;
};

struct SmallObject {
    std::uint64_t cookie;
    unsigned char payload[12];
};

struct MediumObject {
    std::uint64_t cookie;
    unsigned char payload[32];
};

struct LargeObject {
    std::uint64_t cookie;
    unsigned char payload[72];
};

enum Scenario {
    SCENARIO_SMALL,
    SCENARIO_MIXED,
    SCENARIO_LIVE
};

enum Allocator {
    ALLOCATOR_POOL,
    ALLOCATOR_NEW
};

struct Options {
    std::size_t iterations;
    std::size_t samples;
    std::size_t warmup_samples;
    std::size_t live_count;
    std::vector<std::size_t> threads;
    std::vector<Scenario> scenarios;
    std::vector<Allocator> allocators;
    std::string csv_path;
    bool verbose;
};

struct Summary {
    Scenario scenario;
    Allocator allocator;
    std::size_t threads;
    std::size_t allocations;
    double average_ms;
    double median_ms;
    double p95_ms;
    double maximum_ms;
    double allocations_per_second;
};

struct RunningStatistics {
    double seconds;
    std::size_t allocations;
    std::size_t errors;
};

std::string ScenarioName(Scenario scenario)
{
    switch (scenario) {
    case SCENARIO_SMALL:
        return "small";
    case SCENARIO_MIXED:
        return "mixed";
    case SCENARIO_LIVE:
        return "live";
    }
    return "unknown";
}

std::string AllocatorName(Allocator allocator)
{
    return allocator == ALLOCATOR_POOL ? "pool" : "new";
}

void PrintUsage(const char* program)
{
    std::cout
        << "Usage: " << program << " [options]\n\n"
        << "Options:\n"
        << "  --threads LIST       Comma-separated thread counts "
           "(default: 1,2,4,...)\n"
        << "  --iterations N       Alloc/free operations per thread per sample "
           "(default: 100000)\n"
        << "  --samples N          Measured samples (default: 5)\n"
        << "  --warmup N           Warmup samples per configuration "
           "(default: 1)\n"
        << "  --live-count N       Live objects per live-set batch "
           "(default: 1024)\n"
        << "  --scenario LIST      small,mixed,live,all (default: all)\n"
        << "  --allocator LIST     pool,new,all (default: all)\n"
        << "  --csv PATH           Write raw summaries as CSV\n"
        << "  --verbose            Print every measured sample\n"
        << "  --help               Show this help\n\n"
        << "Each operation includes one allocation, writes to the object, "
           "and one matching release.\n";
}

bool StartsWith(const std::string& value, const std::string& prefix)
{
    return value.size() >= prefix.size() &&
           value.compare(0, prefix.size(), prefix) == 0;
}

std::string RequireOptionValue(
    const std::vector<std::string>& arguments,
    std::size_t* index,
    const std::string& option)
{
    const std::string& current = arguments[*index];
    const std::string prefix = option + "=";
    if (StartsWith(current, prefix)) {
        return current.substr(prefix.size());
    }

    ++(*index);
    if (*index >= arguments.size()) {
        throw std::invalid_argument("missing value for " + option);
    }
    return arguments[*index];
}

std::size_t ParseSize(const std::string& text, const std::string& option)
{
    if (!text.empty() && text[0] == '-') {
        throw std::invalid_argument("invalid positive integer for " + option +
                                    ": " + text);
    }

    char* end = nullptr;
    const unsigned long long value =
        std::strtoull(text.c_str(), &end, 10);
    if (text.empty() || end == text.c_str() || *end != '\0' || value == 0 ||
        value > static_cast<unsigned long long>(
                    std::numeric_limits<std::size_t>::max())) {
        throw std::invalid_argument("invalid positive integer for " + option +
                                    ": " + text);
    }
    return static_cast<std::size_t>(value);
}

std::vector<std::size_t> ParseThreads(const std::string& text)
{
    std::vector<std::size_t> result;
    std::stringstream stream(text);
    std::string item;

    while (std::getline(stream, item, ',')) {
        const std::size_t value = ParseSize(item, "--threads");
        if (value > 256) {
            throw std::invalid_argument("--threads cannot exceed 256");
        }
        result.push_back(value);
    }

    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    if (result.empty()) {
        throw std::invalid_argument("--threads requires at least one value");
    }
    return result;
}

std::vector<Scenario> ParseScenarios(const std::string& text)
{
    std::vector<Scenario> result;
    std::stringstream stream(text);
    std::string item;

    while (std::getline(stream, item, ',')) {
        if (item == "all") {
            return {SCENARIO_SMALL, SCENARIO_MIXED, SCENARIO_LIVE};
        }
        if (item == "small") {
            result.push_back(SCENARIO_SMALL);
        } else if (item == "mixed") {
            result.push_back(SCENARIO_MIXED);
        } else if (item == "live") {
            result.push_back(SCENARIO_LIVE);
        } else {
            throw std::invalid_argument("unknown scenario: " + item);
        }
    }

    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    if (result.empty()) {
        throw std::invalid_argument("--scenario requires at least one value");
    }
    return result;
}

std::vector<Allocator> ParseAllocators(const std::string& text)
{
    std::vector<Allocator> result;
    std::stringstream stream(text);
    std::string item;

    while (std::getline(stream, item, ',')) {
        if (item == "all") {
            return {ALLOCATOR_POOL, ALLOCATOR_NEW};
        }
        if (item == "pool") {
            result.push_back(ALLOCATOR_POOL);
        } else if (item == "new") {
            result.push_back(ALLOCATOR_NEW);
        } else {
            throw std::invalid_argument("unknown allocator: " + item);
        }
    }

    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    if (result.empty()) {
        throw std::invalid_argument("--allocator requires at least one value");
    }
    return result;
}

std::vector<std::size_t> DefaultThreadCounts()
{
    const std::size_t hardware =
        std::max<std::size_t>(1, std::thread::hardware_concurrency());
    const std::size_t limit = std::max<std::size_t>(4, hardware * 2);
    std::vector<std::size_t> result;

    for (std::size_t threads = 1; threads <= limit; threads *= 2) {
        result.push_back(threads);
    }
    if (std::find(result.begin(), result.end(), hardware) == result.end()) {
        result.push_back(hardware);
    }

    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

Options ParseOptions(int argc, char** argv)
{
    Options options;
    options.iterations = 100000;
    options.samples = 5;
    options.warmup_samples = 1;
    options.live_count = 1024;
    options.threads = DefaultThreadCounts();
    options.scenarios = {SCENARIO_SMALL, SCENARIO_MIXED, SCENARIO_LIVE};
    options.allocators = {ALLOCATOR_POOL, ALLOCATOR_NEW};
    options.verbose = false;

    const std::vector<std::string> arguments(argv + 1, argv + argc);
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const std::string& argument = arguments[i];

        if (argument == "--help" || argument == "-h") {
            PrintUsage(argv[0]);
            std::exit(0);
        } else if (argument == "--verbose") {
            options.verbose = true;
        } else if (argument == "--threads" ||
                   StartsWith(argument, "--threads=")) {
            options.threads =
                ParseThreads(RequireOptionValue(arguments, &i, "--threads"));
        } else if (argument == "--iterations" ||
                   StartsWith(argument, "--iterations=")) {
            options.iterations = ParseSize(
                RequireOptionValue(arguments, &i, "--iterations"),
                "--iterations");
        } else if (argument == "--samples" ||
                   StartsWith(argument, "--samples=")) {
            options.samples = ParseSize(
                RequireOptionValue(arguments, &i, "--samples"),
                "--samples");
        } else if (argument == "--warmup" ||
                   StartsWith(argument, "--warmup=")) {
            options.warmup_samples = ParseSize(
                RequireOptionValue(arguments, &i, "--warmup"),
                "--warmup");
        } else if (argument == "--live-count" ||
                   StartsWith(argument, "--live-count=")) {
            options.live_count = ParseSize(
                RequireOptionValue(arguments, &i, "--live-count"),
                "--live-count");
        } else if (argument == "--scenario" ||
                   StartsWith(argument, "--scenario=")) {
            options.scenarios =
                ParseScenarios(RequireOptionValue(arguments, &i, "--scenario"));
        } else if (argument == "--allocator" ||
                   StartsWith(argument, "--allocator=")) {
            options.allocators = ParseAllocators(
                RequireOptionValue(arguments, &i, "--allocator"));
        } else if (argument == "--csv" ||
                   StartsWith(argument, "--csv=")) {
            options.csv_path =
                RequireOptionValue(arguments, &i, "--csv");
        } else {
            throw std::invalid_argument("unknown option: " + argument);
        }
    }

    return options;
}

template <typename T>
void EscapePointer(T* pointer)
{
#if defined(__GNUC__) || defined(__clang__)
    __asm__ __volatile__("" : : "r"(pointer) : "memory");
#else
    static std::atomic<std::uintptr_t> sink(0);
    sink.fetch_xor(reinterpret_cast<std::uintptr_t>(pointer),
                   std::memory_order_relaxed);
#endif
}

template <typename T>
void TouchObject(T* object, std::uint64_t seed)
{
    volatile unsigned char* bytes =
        reinterpret_cast<volatile unsigned char*>(object);
    bytes[0] = static_cast<unsigned char>(seed & 0xffU);
    bytes[sizeof(T) / 2] =
        static_cast<unsigned char>((seed >> 8) & 0xffU);
    bytes[sizeof(T) - 1] =
        static_cast<unsigned char>((seed >> 16) & 0xffU);
    EscapePointer(object);
}

template <typename T>
void PoolCycle(std::uint64_t seed)
{
    T* object = hui::NewElement<T>();
    TouchObject(object, seed);
    hui::DeleteElement(object);
}

template <typename T>
void NewCycle(std::uint64_t seed)
{
    T* object = new T();
    TouchObject(object, seed);
    delete object;
}

void RunSmall(
    Allocator allocator,
    std::size_t iterations,
    std::size_t thread_index)
{
    for (std::size_t i = 0; i < iterations; ++i) {
        const std::uint64_t seed =
            static_cast<std::uint64_t>(thread_index * iterations + i);
        if (allocator == ALLOCATOR_POOL) {
            PoolCycle<TinyObject>(seed);
        } else {
            NewCycle<TinyObject>(seed);
        }
    }
}

void RunMixed(
    Allocator allocator,
    std::size_t iterations,
    std::size_t thread_index)
{
    for (std::size_t i = 0; i < iterations; ++i) {
        const std::uint64_t seed =
            static_cast<std::uint64_t>(thread_index * iterations + i);
        switch (i & 3U) {
        case 0:
            if (allocator == ALLOCATOR_POOL) {
                PoolCycle<TinyObject>(seed);
            } else {
                NewCycle<TinyObject>(seed);
            }
            break;
        case 1:
            if (allocator == ALLOCATOR_POOL) {
                PoolCycle<SmallObject>(seed);
            } else {
                NewCycle<SmallObject>(seed);
            }
            break;
        case 2:
            if (allocator == ALLOCATOR_POOL) {
                PoolCycle<MediumObject>(seed);
            } else {
                NewCycle<MediumObject>(seed);
            }
            break;
        default:
            if (allocator == ALLOCATOR_POOL) {
                PoolCycle<LargeObject>(seed);
            } else {
                NewCycle<LargeObject>(seed);
            }
            break;
        }
    }
}

template <typename T>
void RunLiveBatch(
    Allocator allocator,
    std::size_t count,
    std::size_t base_seed,
    std::vector<T*>* objects)
{
    objects->clear();
    objects->reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        T* object = allocator == ALLOCATOR_POOL
                        ? hui::NewElement<T>()
                        : new T();
        TouchObject(object, static_cast<std::uint64_t>(base_seed + i));
        objects->push_back(object);
    }

    for (std::size_t i = 0; i < objects->size(); ++i) {
        if (allocator == ALLOCATOR_POOL) {
            hui::DeleteElement((*objects)[i]);
        } else {
            delete (*objects)[i];
        }
    }
}

void RunLive(
    Allocator allocator,
    std::size_t iterations,
    std::size_t live_count,
    std::size_t thread_index)
{
    std::vector<LargeObject*> objects;
    objects.reserve(std::min(iterations, live_count));

    std::size_t remaining = iterations;
    std::size_t base_seed = thread_index * iterations;
    while (remaining > 0) {
        const std::size_t batch = std::min(remaining, live_count);
        RunLiveBatch(allocator, batch, base_seed, &objects);
        remaining -= batch;
        base_seed += batch;
    }
}

void RunWorker(
    Scenario scenario,
    Allocator allocator,
    std::size_t iterations,
    std::size_t live_count,
    std::size_t thread_index)
{
    switch (scenario) {
    case SCENARIO_SMALL:
        RunSmall(allocator, iterations, thread_index);
        break;
    case SCENARIO_MIXED:
        RunMixed(allocator, iterations, thread_index);
        break;
    case SCENARIO_LIVE:
        RunLive(allocator, iterations, live_count, thread_index);
        break;
    }
}

RunningStatistics RunOnce(
    Scenario scenario,
    Allocator allocator,
    std::size_t thread_count,
    std::size_t iterations,
    std::size_t live_count)
{
    std::atomic<std::size_t> ready(0);
    std::atomic<bool> start(false);
    std::atomic<std::size_t> errors(0);
    std::vector<std::thread> threads;
    threads.reserve(thread_count);

    for (std::size_t thread_index = 0;
         thread_index < thread_count;
         ++thread_index) {
        threads.push_back(std::thread([&, thread_index]() {
            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            try {
                RunWorker(
                    scenario,
                    allocator,
                    iterations,
                    live_count,
                    thread_index);
            } catch (...) {
                errors.fetch_add(1, std::memory_order_relaxed);
            }
        }));
    }

    while (ready.load(std::memory_order_acquire) != thread_count) {
        std::this_thread::yield();
    }

    const std::chrono::steady_clock::time_point begin =
        std::chrono::steady_clock::now();
    start.store(true, std::memory_order_release);

    for (std::size_t i = 0; i < threads.size(); ++i) {
        threads[i].join();
    }

    const std::chrono::steady_clock::time_point end =
        std::chrono::steady_clock::now();
    const double seconds =
        std::chrono::duration<double>(end - begin).count();
    const RunningStatistics result = {
        seconds,
        thread_count * iterations,
        errors.load(std::memory_order_relaxed)
    };
    return result;
}

double Percentile(std::vector<double> values, double percentile)
{
    std::sort(values.begin(), values.end());
    const double position = percentile * (values.size() - 1);
    const std::size_t lower = static_cast<std::size_t>(std::floor(position));
    const std::size_t upper = static_cast<std::size_t>(std::ceil(position));
    if (lower == upper) {
        return values[lower];
    }
    const double weight = position - lower;
    return values[lower] * (1.0 - weight) + values[upper] * weight;
}

Summary Measure(
    const Options& options,
    Scenario scenario,
    Allocator allocator,
    std::size_t thread_count)
{
    const std::size_t warmup_iterations = std::max<std::size_t>(
        1000, std::min<std::size_t>(options.iterations, 10000));
    for (std::size_t i = 0; i < options.warmup_samples; ++i) {
        const RunningStatistics warmup = RunOnce(
            scenario,
            allocator,
            thread_count,
            warmup_iterations,
            options.live_count);
        if (warmup.errors != 0) {
            throw std::runtime_error("benchmark worker failed during warmup");
        }
    }

    std::vector<double> milliseconds;
    std::size_t allocations = 0;
    milliseconds.reserve(options.samples);

    for (std::size_t sample_index = 0;
         sample_index < options.samples;
         ++sample_index) {
        const RunningStatistics sample = RunOnce(
            scenario,
            allocator,
            thread_count,
            options.iterations,
            options.live_count);
        if (sample.errors != 0) {
            throw std::runtime_error("benchmark worker failed");
        }

        const double sample_ms = sample.seconds * 1000.0;
        milliseconds.push_back(sample_ms);
        allocations = sample.allocations;
        if (options.verbose) {
            std::cout << "    sample " << (sample_index + 1) << ": "
                      << std::fixed << std::setprecision(3) << sample_ms
                      << " ms\n";
        }
    }

    double sum = 0.0;
    for (std::size_t i = 0; i < milliseconds.size(); ++i) {
        sum += milliseconds[i];
    }

    Summary summary;
    summary.scenario = scenario;
    summary.allocator = allocator;
    summary.threads = thread_count;
    summary.allocations = allocations;
    summary.average_ms = sum / milliseconds.size();
    summary.median_ms = Percentile(milliseconds, 0.5);
    summary.p95_ms = Percentile(milliseconds, 0.95);
    summary.maximum_ms =
        *std::max_element(milliseconds.begin(), milliseconds.end());
    summary.allocations_per_second =
        static_cast<double>(allocations) / (summary.median_ms / 1000.0);
    return summary;
}

double FindSpeedup(
    const std::vector<Summary>& summaries,
    Scenario scenario,
    std::size_t thread_count)
{
    double pool_throughput = 0.0;
    double new_throughput = 0.0;

    for (std::size_t i = 0; i < summaries.size(); ++i) {
        if (summaries[i].scenario != scenario ||
            summaries[i].threads != thread_count) {
            continue;
        }
        if (summaries[i].allocator == ALLOCATOR_POOL) {
            pool_throughput = summaries[i].allocations_per_second;
        } else {
            new_throughput = summaries[i].allocations_per_second;
        }
    }

    if (pool_throughput == 0.0 || new_throughput == 0.0) {
        return 0.0;
    }
    return pool_throughput / new_throughput;
}

void WriteCsv(
    const std::string& path,
    const std::vector<Summary>& summaries)
{
    std::ofstream output(path.c_str());
    if (!output) {
        throw std::runtime_error("cannot open CSV output: " + path);
    }

    output << "scenario,allocator,threads,allocations,average_ms,median_ms,"
              "p95_ms,maximum_ms,allocations_per_second\n";
    output << std::fixed << std::setprecision(6);
    for (std::size_t i = 0; i < summaries.size(); ++i) {
        const Summary& summary = summaries[i];
        output << ScenarioName(summary.scenario) << ','
               << AllocatorName(summary.allocator) << ','
               << summary.threads << ','
               << summary.allocations << ','
               << summary.average_ms << ','
               << summary.median_ms << ','
               << summary.p95_ms << ','
               << summary.maximum_ms << ','
               << summary.allocations_per_second << '\n';
    }
}

}  // namespace

int main(int argc, char** argv)
{
    try {
        std::cout << std::unitbuf;
        const Options options = ParseOptions(argc, argv);
        hui::HashTable::InitMemoryPools();

        std::cout << "Memory-pool performance test\n"
                  << "hardware threads: "
                  << std::thread::hardware_concurrency() << '\n'
                  << "iterations/thread/sample: " << options.iterations << '\n'
                  << "samples: " << options.samples
                  << ", warmup samples: " << options.warmup_samples << '\n'
                  << "live count: " << options.live_count << "\n\n";

        std::vector<Summary> summaries;
        for (std::size_t scenario_index = 0;
             scenario_index < options.scenarios.size();
             ++scenario_index) {
            const Scenario scenario = options.scenarios[scenario_index];
            std::cout << "== " << ScenarioName(scenario) << " ==\n";

            for (std::size_t allocator_index = 0;
                 allocator_index < options.allocators.size();
                 ++allocator_index) {
                const Allocator allocator =
                    options.allocators[allocator_index];
                std::cout << "allocator: " << AllocatorName(allocator) << '\n';

                for (std::size_t thread_index = 0;
                     thread_index < options.threads.size();
                     ++thread_index) {
                    const std::size_t thread_count =
                        options.threads[thread_index];
                    std::cout << "  threads " << thread_count << std::endl;
                    summaries.push_back(
                        Measure(options, scenario, allocator, thread_count));
                }
            }
            std::cout << '\n';
        }

        std::cout << std::left
                  << std::setw(10) << "scenario"
                  << std::setw(10) << "allocator"
                  << std::right << std::setw(8) << "threads"
                  << std::setw(14) << "median_ms"
                  << std::setw(14) << "p95_ms"
                  << std::setw(16) << "alloc/s"
                  << std::setw(14) << "pool/new" << '\n';
        std::cout << std::string(86, '-') << '\n';

        for (std::size_t i = 0; i < summaries.size(); ++i) {
            const Summary& summary = summaries[i];
            const double speedup =
                FindSpeedup(summaries, summary.scenario, summary.threads);
            std::cout << std::left
                      << std::setw(10) << ScenarioName(summary.scenario)
                      << std::setw(10) << AllocatorName(summary.allocator)
                      << std::right << std::setw(8) << summary.threads
                      << std::fixed << std::setprecision(3)
                      << std::setw(14) << summary.median_ms
                      << std::setw(14) << summary.p95_ms
                      << std::setprecision(0)
                      << std::setw(16) << summary.allocations_per_second;
            if (summary.allocator == ALLOCATOR_POOL && speedup > 0.0) {
                std::cout << std::setprecision(3)
                          << std::setw(14) << speedup;
            } else {
                std::cout << std::setw(14) << "-";
            }
            std::cout << '\n';
        }

        const std::size_t max_requested_threads =
            *std::max_element(options.threads.begin(), options.threads.end());
        if (max_requested_threads >
            std::max<std::size_t>(1, std::thread::hardware_concurrency())) {
            std::cout
                << "\nNote: requested thread count exceeds available hardware "
                   "threads; oversubscription and scheduler noise are included.\n";
        }

        if (!options.csv_path.empty()) {
            WriteCsv(options.csv_path, summaries);
            std::cout << "CSV written to " << options.csv_path << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n"
                  << "Run with --help for usage.\n";
        return 1;
    }
}

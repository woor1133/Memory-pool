#pragma once

#include <exception>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace memory_pool_test {

class Failure : public std::runtime_error {
public:
    explicit Failure(const std::string& message)
        : std::runtime_error(message) {}
};

inline void Check(
    bool condition,
    const char* expression,
    const char* file,
    int line)
{
    if (condition) {
        return;
    }

    std::ostringstream message;
    message << file << ':' << line << ": CHECK(" << expression << ") failed";
    throw Failure(message.str());
}

template <typename Left, typename Right>
void CheckEqual(
    const Left& left,
    const Right& right,
    const char* left_expression,
    const char* right_expression,
    const char* file,
    int line)
{
    if (left == right) {
        return;
    }

    std::ostringstream message;
    message << file << ':' << line << ": CHECK_EQ("
            << left_expression << ", " << right_expression << ") failed\n"
            << "  left:  " << left << '\n'
            << "  right: " << right;
    throw Failure(message.str());
}

struct TestCase {
    const char* name;
    void (*function)();
};

inline int RunCases(const std::vector<TestCase>& cases)
{
    int failures = 0;

    for (std::vector<TestCase>::const_iterator it = cases.begin();
         it != cases.end();
         ++it) {
        try {
            it->function();
            std::cout << "[PASS] " << it->name << '\n' << std::flush;
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[FAIL] " << it->name << '\n'
                      << "       " << error.what() << '\n';
        } catch (...) {
            ++failures;
            std::cerr << "[FAIL] " << it->name
                      << "\n       unknown exception\n";
        }
    }

    std::cout << '\n'
              << (cases.size() - static_cast<std::size_t>(failures))
              << " passed, " << failures << " failed\n"
              << std::flush;

    return failures == 0 ? 0 : 1;
}

}  // namespace memory_pool_test

#define CHECK(expression) \
    ::memory_pool_test::Check( \
        static_cast<bool>(expression), #expression, __FILE__, __LINE__)

#define CHECK_EQ(left, right) \
    ::memory_pool_test::CheckEqual( \
        (left), (right), #left, #right, __FILE__, __LINE__)

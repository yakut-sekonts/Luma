#include "platform/macos/InferenceStack.h"
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
[[gnu::noinline]] unsigned exerciseStack() {
    volatile unsigned char pages[1024 * 1024];
    unsigned result = 0;
    for (std::size_t i = 0; i < sizeof(pages); i += 4096) {
        pages[i] = 1;
        result += pages[i];
    }
    return result;
}
void check() {
    const auto caller = std::this_thread::get_id();
    const auto callerSize = pthread_get_stacksize_np(pthread_self());
    bool completed = false;
    luma::macos::withInferenceStack([&] {
        require(pthread_get_stacksize_np(pthread_self()) >= 8 * 1024 * 1024,
                "Inference stack remains too small");
        require((std::this_thread::get_id() == caller) == (callerSize >= 8 * 1024 * 1024),
                "Inference did not preserve an adequate caller or isolate a small stack");
        require(exerciseStack() == 256, "One MiB stack page exercise failed");
        const auto worker = std::this_thread::get_id();
        luma::macos::withInferenceStack([&] {
            require(std::this_thread::get_id() == worker,
                    "Nested inference created another worker");
        });
        completed = true;
    });
    require(completed, "Inference returned before the borrowed callback completed");
    bool failed = false;
    try {
        luma::macos::withInferenceStack([] { throw std::runtime_error("preserved failure"); });
    } catch (const std::runtime_error &error) {
        failed = std::string(error.what()) == "preserved failure";
    }
    require(failed, "Worker exception did not reach the original caller");
    luma::macos::withInferenceStack([] {});
}
} // namespace
int main() {
    try {
        check();
        auto first = std::async(std::launch::async, check);
        auto second = std::async(std::launch::async, check);
        first.get();
        second.get();
        std::cout << "Inference stack, join, nested calls, concurrent calls and exceptions PASS\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

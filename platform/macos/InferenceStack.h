#pragma once
#include <cstddef>
#include <exception>
#include <functional>
#include <pthread.h>
#include <system_error>

namespace luma::macos {
// BNNS inference on macOS 15 can exceed a secondary pthread's default 512 KiB
// stack. Match the main thread's 8 MiB reservation, with pages committed on use.
// The synchronous join keeps every borrowed tensor/callback alive until return.
inline void withInferenceStack(const std::function<void()> &operation) {
    constexpr std::size_t stackBytes = 8 * 1024 * 1024;
    if (pthread_get_stacksize_np(pthread_self()) >= stackBytes) {
        operation();
        return;
    }
    struct Work {
        const std::function<void()> &operation;
        std::exception_ptr failure;
    } work{operation, {}};
    pthread_attr_t attributes;
    int error = pthread_attr_init(&attributes);
    if (error)
        throw std::system_error(error, std::generic_category(), "Cannot prepare inference thread");
    error = pthread_attr_setstacksize(&attributes, stackBytes);
    pthread_t thread{};
    if (!error)
        error = pthread_create(
            &thread, &attributes,
            [](void *context) noexcept -> void * {
                auto &work = *static_cast<Work *>(context);
                try {
                    work.operation();
                } catch (...) {
                    work.failure = std::current_exception();
                }
                return nullptr;
            },
            &work);
    pthread_attr_destroy(&attributes);
    if (error)
        throw std::system_error(error, std::generic_category(), "Cannot start inference thread");
    // A valid, owned joinable thread must join. Returning after an impossible
    // ownership failure would expose borrowed buffers to a still-running worker.
    if (pthread_join(thread, nullptr) != 0)
        std::terminate();
    if (work.failure)
        std::rethrow_exception(work.failure);
}
} // namespace luma::macos

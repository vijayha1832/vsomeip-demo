#pragma once

#include <vsomeip/vsomeip.hpp>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <pthread.h>
#include <system_error>
#include <thread>

// Construct before creating the application so all its threads inherit the mask.
// sigtimedwait handles shutdown on an ordinary thread, where stop() is safe.
class ShutdownSignals {
public:
    ShutdownSignals() {
        sigemptyset(&signals_);
        sigaddset(&signals_, SIGINT);
        sigaddset(&signals_, SIGTERM);
        const int error = pthread_sigmask(SIG_BLOCK, &signals_, &previous_mask_);
        if (error != 0) {
            throw std::system_error(error, std::generic_category(), "Blocking shutdown signals");
        }
    }

    ~ShutdownSignals() {
        finish();
        pthread_sigmask(SIG_SETMASK, &previous_mask_, nullptr);
    }

    void run(const std::shared_ptr<vsomeip::application>& app) {
        waiting_ = true;
        thread_ = std::thread([this, app]() {
            while (waiting_) {
                const timespec timeout{0, 100000000};
                const int signal = sigtimedwait(&signals_, nullptr, &timeout);
                if (signal == SIGINT || signal == SIGTERM) {
                    app->stop();
                    return;
                }
                if (signal < 0 && errno != EAGAIN && errno != EINTR) {
                    app->stop();
                    return;
                }
            }
        });
        app->start();
        finish();
    }

private:
    void finish() {
        waiting_ = false;
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    sigset_t signals_{};
    sigset_t previous_mask_{};
    std::atomic<bool> waiting_{false};
    std::thread thread_;
};

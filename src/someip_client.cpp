// SOME/IP Client (Consumer) — subscribes to events and sends periodic requests
#include "someip_common.hpp"
#include "shutdown_signals.hpp"
#include <cstdlib>
#include <cstdint>
#include <set>
#include <sstream>
#include <iostream>
#include <thread>
#include <chrono>
#include <condition_variable>
#include <mutex>

class SomeIPClient {
public:
    SomeIPClient()
        : app_(vsomeip::runtime::get()->create_application("SomeIP_Client")),
          service_available_(false),
          running_(false),
          request_counter_(0) {}

    ~SomeIPClient() {
        stop_worker();
    }

    bool init() {
        if (!app_->init()) {
            std::cerr << "[CLIENT] Initialization failed.\n";
            return false;
        }

        // Availability handler — called when service comes up or goes down
        app_->register_availability_handler(
            SERVICE_ID, INSTANCE_ID,
            [this](vsomeip::service_t s, vsomeip::instance_t i, bool available) {
                std::ostringstream status;
                status << "[CLIENT] Service 0x" << std::hex << s << "/0x" << i
                       << (available ? " is AVAILABLE" : " is UNAVAILABLE") << "\n";
                std::cout << status.str();
                {
                    std::lock_guard<std::mutex> lk(mutex_);
                    service_available_ = available;
                }
                cv_.notify_all();
            });

        // Handler for request responses (METHOD_ID)
        app_->register_message_handler(
            SERVICE_ID, INSTANCE_ID, METHOD_ID,
            [](const std::shared_ptr<vsomeip::message>& response) {
                if (response->get_message_type() == vsomeip::message_type_e::MT_ERROR ||
                    response->get_return_code() != vsomeip::return_code_e::E_OK) {
                    std::cerr << "[CLIENT] Request failed with return code "
                              << static_cast<unsigned>(response->get_return_code()) << "\n";
                    return;
                }
                if (response->get_message_type() != vsomeip::message_type_e::MT_RESPONSE) {
                    return;
                }
                const std::string data = payload_text(response->get_payload());
                std::cout << "[CLIENT] Response received: \"" << data << "\"\n";
            });

        // Handler for incoming event notifications
        app_->register_message_handler(
            SERVICE_ID, INSTANCE_ID, EVENT_ID,
            [](const std::shared_ptr<vsomeip::message>& notification) {
                if (notification->get_message_type() != vsomeip::message_type_e::MT_NOTIFICATION) {
                    return;
                }
                const std::string data = payload_text(notification->get_payload());
                std::cout << "[CLIENT] Event notification: \"" << data << "\"\n";
            });

        // State handler — subscribe to events once registered
        app_->register_state_handler(
            [this](vsomeip::state_type_e state) {
                if (state == vsomeip::state_type_e::ST_REGISTERED) {
                    std::cout << "[CLIENT] Registered with routing manager.\n";
                    // Subscribe to the event group
                    std::set<vsomeip::eventgroup_t> groups = {EVENTGROUP_ID};
                    app_->request_event(SERVICE_ID, INSTANCE_ID, EVENT_ID, groups,
                                        vsomeip::event_type_e::ET_FIELD,
                                        vsomeip::reliability_type_e::RT_UNRELIABLE);
                    app_->request_service(SERVICE_ID, INSTANCE_ID);
                    app_->subscribe(SERVICE_ID, INSTANCE_ID, EVENTGROUP_ID);
                } else {
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        service_available_ = false;
                    }
                    cv_.notify_all();
                }
            });

        return true;
    }

    void start(ShutdownSignals& signals) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            running_ = true;
        }
        // Start request-sending thread (waits for service to become available first)
        sender_thread_ = std::thread([this]() { send_requests(); });
        signals.run(app_); // blocks until shutdown
        stop_worker();
        app_->clear_all_handler();
        app_->unsubscribe(SERVICE_ID, INSTANCE_ID, EVENTGROUP_ID);
        app_->release_event(SERVICE_ID, INSTANCE_ID, EVENT_ID);
        app_->release_service(SERVICE_ID, INSTANCE_ID);
    }

private:
    void stop_worker() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            running_ = false;
        }
        cv_.notify_all();
        if (sender_thread_.joinable()) {
            sender_thread_.join();
        }
    }

    void send_requests() {
        std::unique_lock<std::mutex> lock(mutex_);
        while (running_) {
            cv_.wait(lock, [this]() { return !running_ || service_available_; });
            if (!running_) {
                break;
            }
            if (cv_.wait_for(lock, std::chrono::seconds(3),
                             [this]() { return !running_ || !service_available_; })) {
                continue;
            }

            // Build and send a request
            auto request = vsomeip::runtime::get()->create_request(false);
            request->set_service(SERVICE_ID);
            request->set_instance(INSTANCE_ID);
            request->set_method(METHOD_ID);

            std::string payload_str = "HELLO_FROM_CLIENT_" + std::to_string(request_counter_++);
            auto payload = vsomeip::runtime::get()->create_payload();
            payload->set_data(
                reinterpret_cast<const vsomeip::byte_t*>(payload_str.c_str()),
                static_cast<vsomeip::length_t>(payload_str.size()));
            request->set_payload(payload);

            app_->send(request);
            std::cout << "[CLIENT] Sent request: \"" << payload_str << "\"\n";
        }
    }

    std::shared_ptr<vsomeip::application> app_;
    bool service_available_;
    bool running_;
    std::uint32_t request_counter_;
    std::thread sender_thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

int main() {
    ShutdownSignals signals;
    SomeIPClient client;
    if (!client.init()) return EXIT_FAILURE;

    std::cout << "[CLIENT] Starting...\n";
    client.start(signals);
    return EXIT_SUCCESS;
}

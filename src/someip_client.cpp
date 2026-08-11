// SOME/IP Client (Consumer) — subscribes to events and sends periodic requests
#include <vsomeip/vsomeip.hpp>
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <condition_variable>
#include <mutex>

static const vsomeip::service_t    SERVICE_ID   = 0x1234;
static const vsomeip::instance_t   INSTANCE_ID  = 0x5678;
static const vsomeip::method_t     METHOD_ID    = 0x0421;
static const vsomeip::event_t      EVENT_ID     = 0x8001;
static const vsomeip::eventgroup_t EVENTGROUP_ID = 0x0001;

class SomeIPClient {
public:
    SomeIPClient()
        : app_(vsomeip::runtime::get()->create_application("SomeIP_Client")),
          service_available_(false),
          running_(false),
          request_counter_(0) {}

    bool init() {
        if (!app_->init()) {
            std::cerr << "[CLIENT] Initialization failed.\n";
            return false;
        }

        // Availability handler — called when service comes up or goes down
        app_->register_availability_handler(
            SERVICE_ID, INSTANCE_ID,
            [this](vsomeip::service_t s, vsomeip::instance_t i, bool available) {
                std::cout << "[CLIENT] Service 0x" << std::hex << s
                          << "/0x" << i
                          << (available ? " is AVAILABLE" : " is UNAVAILABLE") << "\n";
                {
                    std::lock_guard<std::mutex> lk(mutex_);
                    service_available_ = available;
                }
                cv_.notify_all();
            });

        // Handler for request responses (METHOD_ID)
        app_->register_message_handler(
            SERVICE_ID, INSTANCE_ID, METHOD_ID,
            [this](const std::shared_ptr<vsomeip::message>& response) {
                auto payload = response->get_payload();
                std::string data(reinterpret_cast<const char*>(payload->get_data()),
                                 payload->get_length());
                std::cout << "[CLIENT] Response received: \"" << data << "\"\n";
            });

        // Handler for incoming event notifications
        app_->register_message_handler(
            SERVICE_ID, INSTANCE_ID, EVENT_ID,
            [this](const std::shared_ptr<vsomeip::message>& notification) {
                auto payload = notification->get_payload();
                std::string data(reinterpret_cast<const char*>(payload->get_data()),
                                 payload->get_length());
                std::cout << "[CLIENT] Event notification: \"" << data << "\"\n";
            });

        // State handler — subscribe to events once registered
        app_->register_state_handler(
            [this](vsomeip::state_type_e state) {
                if (state == vsomeip::state_type_e::ST_REGISTERED) {
                    std::cout << "[CLIENT] Registered with routing manager.\n";
                    app_->request_service(SERVICE_ID, INSTANCE_ID);

                    // Subscribe to the event group
                    std::set<vsomeip::eventgroup_t> groups = {EVENTGROUP_ID};
                    app_->request_event(SERVICE_ID, INSTANCE_ID, EVENT_ID, groups,
                                        vsomeip::event_type_e::ET_FIELD);
                    app_->subscribe(SERVICE_ID, INSTANCE_ID, EVENTGROUP_ID);
                }
            });

        return true;
    }

    void start() {
        running_ = true;
        // Start request-sending thread (waits for service to become available first)
        sender_thread_ = std::thread([this]() { send_requests(); });
        app_->start(); // blocks
    }

    void stop() {
        running_ = false;
        app_->unsubscribe(SERVICE_ID, INSTANCE_ID, EVENTGROUP_ID);
        app_->release_event(SERVICE_ID, INSTANCE_ID, EVENT_ID);
        app_->release_service(SERVICE_ID, INSTANCE_ID);
        if (sender_thread_.joinable())
            sender_thread_.join();
        app_->stop();
    }

private:
    void send_requests() {
        // Wait until the service is available
        {
            std::unique_lock<std::mutex> lk(mutex_);
            cv_.wait(lk, [this]() { return service_available_.load(); });
        }
        std::cout << "[CLIENT] Service available — starting to send requests.\n";

        while (running_) {
            std::this_thread::sleep_for(std::chrono::seconds(3));

            if (!service_available_) continue;

            // Build and send a request
            auto request = vsomeip::runtime::get()->create_request();
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
    std::atomic<bool> service_available_;
    std::atomic<bool> running_;
    uint32_t request_counter_;
    std::thread sender_thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

int main() {
    SomeIPClient client;
    if (!client.init()) return EXIT_FAILURE;

    std::cout << "[CLIENT] Starting...\n";
    client.start(); // blocks until app_->stop() is called
    return EXIT_SUCCESS;
}

// SOME/IP Service (Provider) — offers a service and sends periodic events
#include "someip_common.hpp"
#include "shutdown_signals.hpp"
#include <vsomeip/defines.hpp>
#include <cstdlib>
#include <cstdint>
#include <set>
#include <iostream>
#include <thread>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <condition_variable>
#include <mutex>

class SomeIPService {
public:
    SomeIPService()
        : app_(vsomeip::runtime::get()->create_application("SomeIP_Service")),
          counter_(0),
          running_(false) {}

    ~SomeIPService() {
        stop_worker();
    }

    bool init() {
        if (!app_->init()) {
            std::cerr << "[SERVICE] Initialization failed.\n";
            return false;
        }

        // Register a handler for incoming requests on METHOD_ID
        app_->register_message_handler(
            SERVICE_ID, INSTANCE_ID, METHOD_ID,
            [this](const std::shared_ptr<vsomeip::message>& request) {
                handle_request(request);
            });

        const std::set<vsomeip::eventgroup_t> groups = {EVENTGROUP_ID};
        app_->offer_event(SERVICE_ID, INSTANCE_ID, EVENT_ID, groups,
                          vsomeip::event_type_e::ET_FIELD);

        // Register state handler
        app_->register_state_handler(
            [this](vsomeip::state_type_e state) {
                std::lock_guard<std::mutex> lock(mutex_);
                registered_ = state == vsomeip::state_type_e::ST_REGISTERED;
                if (registered_) {
                    std::cout << "[SERVICE] Registered with routing manager.\n";
                    offer();
                }
                cv_.notify_all();
            });

        return true;
    }

    void start(ShutdownSignals& signals) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            running_ = true;
        }
        // Start event publishing thread
        publisher_thread_ = std::thread([this]() { publish_events(); });
        signals.run(app_);
        stop_worker();
        app_->clear_all_handler();
        app_->stop_offer_service(SERVICE_ID, INSTANCE_ID);
        app_->stop_offer_event(SERVICE_ID, INSTANCE_ID, EVENT_ID);
    }

private:
    void stop_worker() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            running_ = false;
        }
        cv_.notify_all();
        if (publisher_thread_.joinable()) {
            publisher_thread_.join();
        }
    }

    void offer() {
        app_->offer_service(SERVICE_ID, INSTANCE_ID);
        std::ostringstream status;
        status << "[SERVICE] Service 0x" << std::hex << SERVICE_ID
               << "/0x" << INSTANCE_ID << " offered.\n";
        std::cout << status.str();
    }

    // Handle request-response: echo the payload back with a server prefix
    void handle_request(const std::shared_ptr<vsomeip::message>& request) {
        if (request->get_message_type() != vsomeip::message_type_e::MT_REQUEST) {
            return;
        }
        const std::string received = payload_text(request->get_payload());
        std::cout << "[SERVICE] Received request: \"" << received << "\"\n";

        // Build response
        auto response = vsomeip::runtime::get()->create_response(request);
        std::string reply = "SERVICE_ECHO: " + received;
        if (reply.size() > VSOMEIP_MAX_UDP_MESSAGE_SIZE - VSOMEIP_FULL_HEADER_SIZE) {
            response->set_message_type(vsomeip::message_type_e::MT_ERROR);
            response->set_return_code(vsomeip::return_code_e::E_NOT_OK);
            app_->send(response);
            std::cerr << "[SERVICE] Echo response exceeds the UDP payload limit.\n";
            return;
        }
        auto resp_payload = vsomeip::runtime::get()->create_payload();
        resp_payload->set_data(
            reinterpret_cast<const vsomeip::byte_t*>(reply.c_str()),
            static_cast<vsomeip::length_t>(reply.size()));
        response->set_payload(resp_payload);
        app_->send(response);

        std::cout << "[SERVICE] Sent response: \"" << reply << "\"\n";
    }

    // Periodically fire notification events carrying an incrementing counter
    void publish_events() {
        std::unique_lock<std::mutex> lock(mutex_);
        while (running_) {
            cv_.wait(lock, [this]() { return !running_ || registered_; });
            if (!running_) {
                break;
            }
            if (cv_.wait_for(lock, std::chrono::seconds(2),
                             [this]() { return !running_ || !registered_; })) {
                continue;
            }

            std::ostringstream oss;
            oss << "EVENT_DATA_" << std::setw(4) << std::setfill('0') << counter_++;

            auto event_payload = vsomeip::runtime::get()->create_payload();
            std::string data = oss.str();
            event_payload->set_data(
                reinterpret_cast<const vsomeip::byte_t*>(data.c_str()),
                static_cast<vsomeip::length_t>(data.size()));

            app_->notify(SERVICE_ID, INSTANCE_ID, EVENT_ID, event_payload);
            std::cout << "[SERVICE] Notified event: \"" << data << "\"\n";
        }
    }

    std::shared_ptr<vsomeip::application> app_;
    std::uint32_t counter_;
    bool running_;
    bool registered_ = false;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread publisher_thread_;
};

int main() {
    ShutdownSignals signals;
    SomeIPService service;
    if (!service.init()) return EXIT_FAILURE;

    std::cout << "[SERVICE] Starting...\n";
    service.start(signals);
    return EXIT_SUCCESS;
}

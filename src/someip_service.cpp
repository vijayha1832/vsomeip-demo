// SOME/IP Service (Provider) — offers a service and sends periodic events
#include <vsomeip/vsomeip.hpp>
#include <iostream>
#include <thread>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <atomic>
#include <condition_variable>
#include <mutex>

static const vsomeip::service_t  SERVICE_ID    = 0x1234;
static const vsomeip::instance_t INSTANCE_ID   = 0x5678;
static const vsomeip::method_t   METHOD_ID     = 0x0421; // request/response
static const vsomeip::event_t    EVENT_ID      = 0x8001; // notification event
static const vsomeip::eventgroup_t EVENTGROUP_ID = 0x0001;

class SomeIPService {
public:
    SomeIPService()
        : app_(vsomeip::runtime::get()->create_application("SomeIP_Service")),
          counter_(0),
          running_(false) {}

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

        // Register state handler
        app_->register_state_handler(
            [this](vsomeip::state_type_e state) {
                if (state == vsomeip::state_type_e::ST_REGISTERED) {
                    std::cout << "[SERVICE] Registered with routing manager.\n";
                    offer();
                }
            });

        return true;
    }

    void start() {
        running_ = true;
        // Start event publishing thread
        publisher_thread_ = std::thread([this]() { publish_events(); });
        app_->start();
    }

    void stop() {
        running_ = false;
        app_->stop_offer_service(SERVICE_ID, INSTANCE_ID);
        app_->release_event(SERVICE_ID, INSTANCE_ID, EVENT_ID);
        if (publisher_thread_.joinable())
            publisher_thread_.join();
        app_->stop();
    }

private:
    void offer() {
        // Offer the event before offering the service
        std::set<vsomeip::eventgroup_t> groups = {EVENTGROUP_ID};
        app_->offer_event(SERVICE_ID, INSTANCE_ID, EVENT_ID, groups,
                          vsomeip::event_type_e::ET_FIELD);

        app_->offer_service(SERVICE_ID, INSTANCE_ID);
        std::cout << "[SERVICE] Service 0x" << std::hex << SERVICE_ID
                  << "/0x" << INSTANCE_ID << " offered.\n";
    }

    // Handle request-response: echo the payload back with a server prefix
    void handle_request(const std::shared_ptr<vsomeip::message>& request) {
        auto payload = request->get_payload();
        std::string received(reinterpret_cast<const char*>(payload->get_data()),
                             payload->get_length());
        std::cout << "[SERVICE] Received request: \"" << received << "\"\n";

        // Build response
        auto response = vsomeip::runtime::get()->create_response(request);
        std::string reply = "SERVICE_ECHO: " + received;
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
        while (running_) {
            std::this_thread::sleep_for(std::chrono::seconds(2));

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
    uint32_t counter_;
    std::atomic<bool> running_;
    std::thread publisher_thread_;
};

int main() {
    SomeIPService service;
    if (!service.init()) return EXIT_FAILURE;

    std::cout << "[SERVICE] Starting...\n";
    service.start(); // blocks until app_->stop() is called
    return EXIT_SUCCESS;
}

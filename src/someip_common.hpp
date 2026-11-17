#pragma once

#include <vsomeip/vsomeip.hpp>
#include <string>

constexpr vsomeip::service_t SERVICE_ID = 0x1234;
constexpr vsomeip::instance_t INSTANCE_ID = 0x5678;
constexpr vsomeip::method_t METHOD_ID = 0x0421;
constexpr vsomeip::event_t EVENT_ID = 0x8001;
constexpr vsomeip::eventgroup_t EVENTGROUP_ID = 0x0001;

inline std::string payload_text(const std::shared_ptr<vsomeip::payload>& payload) {
    if (!payload || payload->get_length() == 0) {
        return {};
    }
    return {reinterpret_cast<const char*>(payload->get_data()), payload->get_length()};
}

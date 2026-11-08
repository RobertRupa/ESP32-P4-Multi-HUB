#include "web_zigbee_approval.hpp"

#include <cassert>
#include <cstdint>

namespace {

std::int64_t now_us = 0;

void test_web_approval_lifecycle() {
    using hub::diagnostics::WebZigbeeAction;
    using hub::diagnostics::WebZigbeeApproval;
    using hub::diagnostics::WebZigbeeRequestSnapshot;
    using hub::diagnostics::WebZigbeeRequestState;

    WebZigbeeApproval approval;
    std::uint32_t request_id = 0;
    assert(approval.request(WebZigbeeAction::create_network, &request_id) == ESP_OK);
    assert(request_id != 0);
    assert(approval.request(WebZigbeeAction::open_join_window, &request_id) ==
           ESP_ERR_INVALID_STATE);

    const std::uint32_t approved_id = request_id;
    assert(approval.approve(approved_id) == ESP_OK);
    assert(approval.approve(approved_id) == ESP_ERR_INVALID_STATE);

    WebZigbeeAction action = WebZigbeeAction::none;
    assert(approval.claim(approved_id, &action));
    assert(action == WebZigbeeAction::create_network);
    assert(!approval.claim(approved_id, &action));
    assert(approval.complete(approved_id, ESP_OK) == ESP_OK);

    WebZigbeeRequestSnapshot snapshot{};
    assert(approval.read_snapshot(&snapshot));
    assert(snapshot.state == WebZigbeeRequestState::completed);
}

void test_touchscreen_approval_and_rejection() {
    using hub::diagnostics::WebZigbeeAction;
    using hub::diagnostics::WebZigbeeApproval;
    using hub::diagnostics::WebZigbeeRequestSnapshot;
    using hub::diagnostics::WebZigbeeRequestState;

    WebZigbeeApproval approval;
    std::uint32_t request_id = 0;
    assert(approval.request(WebZigbeeAction::open_join_window, &request_id) == ESP_OK);

    WebZigbeeAction action = WebZigbeeAction::none;
    assert(approval.claim(request_id, &action));
    assert(action == WebZigbeeAction::open_join_window);
    assert(approval.complete(request_id, ESP_ERR_TIMEOUT) == ESP_OK);

    assert(approval.request(WebZigbeeAction::create_network, &request_id) == ESP_OK);
    assert(approval.reject(request_id) == ESP_OK);
    assert(approval.reject(request_id) == ESP_ERR_INVALID_STATE);

    WebZigbeeRequestSnapshot snapshot{};
    assert(approval.read_snapshot(&snapshot));
    assert(snapshot.state == WebZigbeeRequestState::rejected);
}

void test_scan_nearby_approval() {
    using hub::diagnostics::WebZigbeeAction;
    using hub::diagnostics::WebZigbeeApproval;
    using hub::diagnostics::WebZigbeeRequestSnapshot;
    using hub::diagnostics::WebZigbeeRequestState;

    WebZigbeeApproval approval;
    std::uint32_t request_id = 0;
    assert(approval.request(WebZigbeeAction::scan_nearby, &request_id) == ESP_OK);
    assert(approval.request(WebZigbeeAction::create_network, &request_id) ==
           ESP_ERR_INVALID_STATE);
    assert(approval.approve(request_id) == ESP_OK);

    WebZigbeeAction action = WebZigbeeAction::none;
    assert(approval.claim(request_id, &action));
    assert(action == WebZigbeeAction::scan_nearby);
    assert(approval.complete(request_id, ESP_OK) == ESP_OK);

    WebZigbeeRequestSnapshot snapshot{};
    assert(approval.read_snapshot(&snapshot));
    assert(snapshot.state == WebZigbeeRequestState::completed);
}

void test_expiry_and_stale_ids() {
    using hub::diagnostics::WebZigbeeAction;
    using hub::diagnostics::WebZigbeeApproval;
    using hub::diagnostics::WebZigbeeRequestSnapshot;
    using hub::diagnostics::WebZigbeeRequestState;

    constexpr std::int64_t kRequestLifetimeUs = 60000000;
    WebZigbeeApproval approval;
    std::uint32_t request_id = 0;
    assert(approval.request(WebZigbeeAction::create_network, &request_id) == ESP_OK);
    const std::uint32_t stale_id = request_id + 1U;
    assert(approval.approve(stale_id) == ESP_ERR_INVALID_STATE);

    now_us += kRequestLifetimeUs;
    assert(approval.approve(request_id) == ESP_ERR_TIMEOUT);
    assert(approval.reject(request_id) == ESP_ERR_INVALID_STATE);

    WebZigbeeRequestSnapshot snapshot{};
    assert(approval.read_snapshot(&snapshot));
    assert(snapshot.state == WebZigbeeRequestState::expired);

    now_us += 1;
    assert(approval.request(WebZigbeeAction::open_join_window, &request_id) == ESP_OK);
    assert(approval.reject(request_id) == ESP_OK);

    WebZigbeeApproval expired_rejection;
    assert(expired_rejection.request(WebZigbeeAction::create_network,
                                     &request_id) == ESP_OK);
    now_us += kRequestLifetimeUs;
    assert(expired_rejection.reject(request_id) == ESP_ERR_TIMEOUT);
    assert(expired_rejection.read_snapshot(&snapshot));
    assert(snapshot.state == WebZigbeeRequestState::expired);
}

}

std::int64_t esp_timer_get_time() {
    return now_us;
}

int main() {
    test_web_approval_lifecycle();
    test_touchscreen_approval_and_rejection();
    test_scan_nearby_approval();
    test_expiry_and_stale_ids();
}
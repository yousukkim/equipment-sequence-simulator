#pragma once

#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "model/types.hpp"

namespace ess {

// 시각이 모두 확정된 시나리오를, 검증하기 좋은 형태로 한 번만 색인해 둔 읽기 전용 뷰.
// 규칙마다 같은 정렬·그룹핑을 반복하지 않게 하려는 목적이다.
//
// 수명: 원본 Scenario를 참조만 하므로, 뷰보다 오래 살아 있어야 한다.
class ScheduleView {
public:
    // start_ms가 비어 있는 동작들의 id. 비어 있지 않으면 검증할 수 없다.
    static std::vector<std::string> unplaced_steps(const Scenario& scenario);

    // 사전 조건: 로더를 통과했고(참조 무결성 보장), 모든 동작에 start_ms가 있다.
    explicit ScheduleView(const Scenario& scenario);

    // 시작 시각 순으로 정렬된 전체 동작.
    std::span<const PlacedStep> placed() const { return placed_; }

    // 특정 자원 위의 동작만, 역시 시작 시각 순으로.
    std::span<const PlacedStep> on_resource(const std::string& resource_id) const;

    const Scenario& scenario() const { return *scenario_; }
    const Resource& resource(const std::string& resource_id) const;
    const PlacedStep* placement_of(const std::string& step_id) const;

private:
    const Scenario* scenario_;
    std::vector<PlacedStep> placed_;
    std::unordered_map<std::string, std::vector<PlacedStep>> by_resource_;
    std::unordered_map<std::string, const Resource*> resource_index_;
    std::unordered_map<std::string, const PlacedStep*> placement_index_;
};

}  // namespace ess

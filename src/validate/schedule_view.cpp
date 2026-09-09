#include "validate/schedule_view.hpp"

#include <algorithm>
#include <stdexcept>

namespace ess {

std::vector<std::string> ScheduleView::unplaced_steps(const Scenario& scenario) {
    std::vector<std::string> out;
    for (const Step& s : scenario.steps) {
        if (!s.start_ms.has_value()) out.push_back(s.id);
    }
    return out;
}

ScheduleView::ScheduleView(const Scenario& scenario) : scenario_(&scenario) {
    for (const Resource& r : scenario.resources) {
        resource_index_.emplace(r.id, &r);
        by_resource_.emplace(r.id, std::vector<PlacedStep>{});
    }

    placed_.reserve(scenario.steps.size());
    for (const Step& s : scenario.steps) {
        if (!s.start_ms.has_value()) {
            throw std::logic_error("ScheduleView: 시각이 배치되지 않은 동작이 있습니다: " + s.id);
        }
        const Ms start = *s.start_ms;
        placed_.push_back(PlacedStep{.when = Interval{.start = start, .end = start + s.duration_ms},
                                     .step = &s});
    }

    // PlacedStep의 <=>가 (구간, id) 순서를 정의하므로 동률에서도 결과가 흔들리지 않는다.
    std::ranges::sort(placed_);

    for (std::size_t i = 0; i < placed_.size(); ++i) {
        const PlacedStep& p = placed_[i];
        placement_index_.emplace(p.step->id, i);
        if (const auto it = by_resource_.find(p.step->resource_id); it != by_resource_.end()) {
            it->second.push_back(p);
        }
    }
}

std::span<const PlacedStep> ScheduleView::on_resource(const std::string& resource_id) const {
    const auto it = by_resource_.find(resource_id);
    if (it == by_resource_.end()) return {};
    return it->second;
}

const Resource& ScheduleView::resource(const std::string& resource_id) const {
    const auto it = resource_index_.find(resource_id);
    if (it == resource_index_.end()) {
        // 로더가 참조 무결성을 보장하므로 여기 오면 프로그래밍 오류다.
        throw std::logic_error("ScheduleView: 알 수 없는 자원 " + resource_id);
    }
    return *it->second;
}

const PlacedStep* ScheduleView::placement_of(const std::string& step_id) const {
    const auto it = placement_index_.find(step_id);
    return it == placement_index_.end() ? nullptr : &placed_[it->second];
}

}  // namespace ess

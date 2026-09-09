#include "schedule/scheduler.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <unordered_map>
#include <vector>

#include "model/dependency_graph.hpp"

namespace ess {
namespace {

// 자원 하나에 대해 배치를 진행하면서 들고 가는 상태.
// run_ms / limit_reached는 검증기의 DutyRule과 같은 누적 규칙을 따른다.
// 스케줄러가 검증기와 다른 계산을 하면 결과가 검증을 통과하지 못한다.
struct ResourceState {
    bool has_any = false;
    Ms last_end = 0;            // 배치는 자원별로 시간 순이므로 항상 최댓값이다
    Ms run_ms = 0;              // 현재 연속 구간에 쌓인 동작 시간
    bool limit_reached = false;
    std::vector<Interval> placed;  // 배타 자원 검사에 쓴다
};

// 배타 관계는 대칭이다. 한쪽만 선언해도 양쪽 모두에 등록한다.
std::unordered_map<std::string, std::vector<std::string>> build_exclusive_partners(
    const Scenario& scenario) {
    std::unordered_map<std::string, std::vector<std::string>> partners;
    for (const Resource& resource : scenario.resources) {
        partners.try_emplace(resource.id);
    }
    for (const Resource& resource : scenario.resources) {
        for (const std::string& other : resource.constraints.exclusive_with) {
            if (other == resource.id || !partners.contains(other)) continue;
            partners[resource.id].push_back(other);
            partners[other].push_back(resource.id);
        }
    }
    for (auto& [id, list] : partners) {
        std::ranges::sort(list);
        list.erase(std::ranges::unique(list).begin(), list.end());
    }
    return partners;
}

}  // namespace

ScheduleResult schedule(const Scenario& scenario) {
    ScheduleResult result{.scenario = scenario, .failures = {}};
    for (Step& step : result.scenario.steps) {
        step.start_ms.reset();  // 새로 배치한다. 입력에 있던 시각은 쓰지 않는다
    }

    std::unordered_map<std::string, const Resource*> resources;
    for (const Resource& resource : scenario.resources) {
        resources.emplace(resource.id, &resource);
    }

    // 0-a. 사전 조건 검사.
    //      참조 무결성은 로더의 책임이지만(DECISIONS D4), 로더를 거치지 않고 직접 부르는 경우가
    //      있다. 조용히 건너뛰면 시각이 비어 있는 동작을 남긴 채 성공을 반환하게 되므로 막는다.
    for (const Step& step : scenario.steps) {
        if (!resources.contains(step.resource_id)) {
            result.input_errors.push_back(std::format(
                "동작 '{}'이(가) 알 수 없는 자원 '{}'을(를) 가리킵니다.", step.id,
                step.resource_id));
        }
    }
    if (!result.input_errors.empty()) return result;

    // 0-b. 시각을 옮겨서는 고칠 수 없는 제약을 본다.
    //      이런 입력은 배치를 시도해 봐야 의미가 없고, 원인도 배치 결과보다 이 시점이 더 명확하다.
    for (const Step& step : scenario.steps) {
        const Constraints& c = resources.at(step.resource_id)->constraints;

        if (step.rate < c.rate_min || step.rate > c.rate_max) {
            result.failures.push_back(Violation{
                .code = ViolationCode::RateOutOfRange,
                .step_id = step.id,
                .resource_id = step.resource_id,
                .detail = std::format(
                    "동작률 {}은(는) 시각을 옮겨도 고칠 수 없습니다. 자원 '{}'의 허용 범위를 "
                    "벗어납니다.",
                    step.rate, step.resource_id),
            });
        }
        if (c.max_continuous_ms > 0 && step.duration_ms > c.max_continuous_ms) {
            result.failures.push_back(Violation{
                .code = ViolationCode::DutyExceeded,
                .step_id = step.id,
                .resource_id = step.resource_id,
                .detail = std::format(
                    "동작 길이 {}ms가 자원 '{}'의 연속 동작 한도 {}ms보다 깁니다. "
                    "어떤 배치로도 만족할 수 없습니다.",
                    step.duration_ms, step.resource_id, c.max_continuous_ms),
            });
        }
    }
    if (!result.failures.empty()) return result;

    // 1. 위상 정렬. 순환이 있으면 실행 순서 자체가 성립하지 않는다.
    const auto order = topological_order(scenario);
    if (!order.has_value()) {
        for (const std::vector<std::string>& cycle : find_dependency_cycles(scenario)) {
            std::string path;
            for (const std::string& id : cycle) {
                if (!path.empty()) path += " → ";
                path += id;
            }
            path += " → " + cycle.front();
            result.failures.push_back(Violation{
                .code = ViolationCode::CyclicDependency,
                .step_id = cycle.front(),
                .detail = std::format("의존 관계가 순환해 실행 순서를 정할 수 없습니다: {}", path),
            });
        }
        return result;
    }

    // 2. 위상 순서대로 하나씩 배치한다.
    const auto partners = build_exclusive_partners(scenario);
    std::unordered_map<std::string, ResourceState> states;
    std::unordered_map<std::string, Ms> end_of;  // 이미 배치된 동작의 종료 시각

    for (const std::size_t index : *order) {
        Step& step = result.scenario.steps[index];
        // 0-a에서 모든 동작의 자원이 존재함을 확인했으므로 여기서는 조회가 반드시 성공한다.
        const Constraints& c = resources.at(step.resource_id)->constraints;
        ResourceState& state = states[step.resource_id];
        const Ms duration = step.duration_ms;

        // 선행 동작이 모두 끝난 뒤라야 한다.
        Ms start = 0;
        for (const std::string& dep : step.depends_on) {
            if (const auto it = end_of.find(dep); it != end_of.end()) {
                start = std::max(start, it->second);
            }
        }

        // 같은 자원의 직전 동작이 끝나고 안정화 시간이 지나야 한다.
        if (state.has_any) {
            start = std::max(start, state.last_end + c.settle_time_ms);
        }

        // 연속 동작 한도에 걸린다면 휴지를 확보해 연속 구간을 끊는다.
        if (c.max_continuous_ms > 0 && state.has_any) {
            const Ms gap = start - state.last_end;
            const bool would_exceed = state.run_ms + duration > c.max_continuous_ms;
            if (gap < c.cooldown_ms && (state.limit_reached || would_exceed)) {
                start = std::max(start, state.last_end + c.cooldown_ms);
            }
        }

        // 배타 자원의 구간과 겹치지 않을 때까지 뒤로 민다.
        // start는 단조 증가하므로 위에서 맞춘 조건들은 다시 깨지지 않는다
        // (뒤로 갈수록 안정화 간격과 휴지 간격은 넓어지기만 한다).
        const std::vector<std::string>& exclusive = partners.at(step.resource_id);
        std::size_t guard = 0;
        const std::size_t guard_limit = scenario.steps.size() + 1;
        for (bool pushed = true; pushed;) {
            pushed = false;
            const Interval candidate{.start = start, .end = start + duration};
            for (const std::string& other : exclusive) {
                for (const Interval& blocked : states[other].placed) {
                    if (candidate.overlaps(blocked)) {
                        start = std::max(start, blocked.end);
                        pushed = true;
                    }
                }
            }
            if (pushed && ++guard > guard_limit) {
                result.failures.push_back(Violation{
                    .code = ViolationCode::ExclusiveConflict,
                    .step_id = step.id,
                    .resource_id = step.resource_id,
                    .detail = "배타 자원을 피할 시각을 찾지 못했습니다.",
                });
                return result;
            }
        }

        // 시각 확정. 자원 상태는 검증기의 DutyRule과 같은 규칙으로 갱신한다.
        const Interval when{.start = start, .end = start + duration};
        step.start_ms = when.start;
        end_of[step.id] = when.end;

        if (c.max_continuous_ms > 0) {
            const Ms gap = when.start - state.last_end;
            if (!state.has_any || gap >= c.cooldown_ms) state.run_ms = 0;
            state.run_ms += duration;
            state.limit_reached = state.run_ms >= c.max_continuous_ms;
        }
        state.last_end = when.end;
        state.has_any = true;
        state.placed.push_back(when);
    }

    return result;
}

}  // namespace ess

#include "validate/rules.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>

namespace ess {
namespace {

// rate_max 기본값은 무한대다. 그대로 찍으면 "inf"가 나오므로 읽히는 말로 바꾼다.
std::string bound_text(double value) {
    if (std::isinf(value)) return value > 0 ? "제한 없음" : "-무한";
    return std::format("{}", value);
}

std::string arrow_join(const std::vector<std::string>& nodes) {
    std::string out;
    for (const std::string& n : nodes) {
        if (!out.empty()) out += " → ";
        out += n;
    }
    return out;
}

// 의존 그래프에서 순환을 찾아낸다.
// 간선 방향은 "선행 → 후행"으로, 메시지가 실행 순서대로 읽히게 했다.
class CycleFinder {
public:
    explicit CycleFinder(std::unordered_map<std::string, std::vector<std::string>> adjacency)
        : adjacency_(std::move(adjacency)) {}

    // 시작점 순서를 호출자가 정해 주면 결과 순서도 결정론적이 된다.
    void visit_all(const std::vector<std::string>& order) {
        for (const std::string& node : order) {
            if (color_[node] == kWhite) visit(node);
        }
    }

    // 중복 없는 순환 목록. 각 순환은 가장 작은 id에서 시작하도록 회전되어 있다.
    const std::set<std::vector<std::string>>& cycles() const { return cycles_; }

private:
    static constexpr int kWhite = 0;  // 미방문
    static constexpr int kGray = 1;   // 현재 경로 위
    static constexpr int kBlack = 2;  // 탐색 완료

    std::unordered_map<std::string, std::vector<std::string>> adjacency_;
    std::unordered_map<std::string, int> color_;
    std::vector<std::string> path_;
    std::set<std::vector<std::string>> cycles_;

    void visit(const std::string& node) {
        color_[node] = kGray;
        path_.push_back(node);
        if (const auto it = adjacency_.find(node); it != adjacency_.end()) {
            for (const std::string& next : it->second) {
                const int color = color_[next];
                if (color == kWhite) {
                    visit(next);
                } else if (color == kGray) {
                    record_cycle(next);
                }
            }
        }
        path_.pop_back();
        color_[node] = kBlack;
    }

    void record_cycle(const std::string& entry) {
        const auto it = std::find(path_.begin(), path_.end(), entry);
        if (it == path_.end()) return;
        std::vector<std::string> cycle(it, path_.end());
        // 같은 순환을 어디서 발견하든 같은 표현이 되도록 최소 id로 회전시킨다.
        const auto smallest = std::min_element(cycle.begin(), cycle.end());
        std::rotate(cycle.begin(), smallest, cycle.end());
        cycles_.insert(std::move(cycle));
    }
};

}  // namespace

void RateRule::check(const ScheduleView& view, std::vector<Violation>& out) const {
    for (const PlacedStep& placed : view.placed()) {
        const Step& step = *placed.step;
        const Constraints& c = view.resource(step.resource_id).constraints;
        if (step.rate < c.rate_min || step.rate > c.rate_max) {
            out.push_back(Violation{
                .code = ViolationCode::RateOutOfRange,
                .step_id = step.id,
                .resource_id = step.resource_id,
                .detail = std::format("동작률 {}이(가) 자원 '{}'의 허용 범위 [{}, {}]를 벗어납니다.",
                                      step.rate, step.resource_id, bound_text(c.rate_min),
                                      bound_text(c.rate_max)),
            });
        }
    }
}

void PrecedenceRule::check(const ScheduleView& view, std::vector<Violation>& out) const {
    for (const PlacedStep& placed : view.placed()) {
        const Step& step = *placed.step;
        for (const std::string& dep_id : step.depends_on) {
            const PlacedStep* dep = view.placement_of(dep_id);
            if (dep == nullptr) continue;  // 로더가 걸러 주는 경우
            if (placed.when.start < dep->when.end) {
                out.push_back(Violation{
                    .code = ViolationCode::PrecedenceViolation,
                    .step_id = step.id,
                    .related_id = dep_id,
                    .resource_id = step.resource_id,
                    .detail = std::format(
                        "선행 동작 '{}'이(가) {}ms에 끝나는데 {}ms에 시작합니다. {}ms 부족합니다.",
                        dep_id, dep->when.end, placed.when.start,
                        dep->when.end - placed.when.start),
                });
            }
        }
    }
}

void CycleRule::check(const ScheduleView& view, std::vector<Violation>& out) const {
    const Scenario& scenario = view.scenario();

    std::unordered_map<std::string, std::vector<std::string>> adjacency;
    std::vector<std::string> order;
    order.reserve(scenario.steps.size());
    for (const Step& step : scenario.steps) {
        order.push_back(step.id);
        adjacency.try_emplace(step.id);
    }
    for (const Step& step : scenario.steps) {
        for (const std::string& dep : step.depends_on) {
            adjacency[dep].push_back(step.id);  // 선행 → 후행
        }
    }

    CycleFinder finder{std::move(adjacency)};
    finder.visit_all(order);

    for (const std::vector<std::string>& cycle : finder.cycles()) {
        std::vector<std::string> shown = cycle;
        shown.push_back(cycle.front());  // 닫힌 고리로 보이게 시작점을 한 번 더 붙인다
        const Step* head = nullptr;
        for (const Step& step : scenario.steps) {
            if (step.id == cycle.front()) head = &step;
        }
        out.push_back(Violation{
            .code = ViolationCode::CyclicDependency,
            .step_id = cycle.front(),
            .resource_id = head != nullptr ? head->resource_id : std::string{},
            .detail = std::format("의존 관계가 순환합니다: {}", arrow_join(shown)),
        });
    }
}

void SettleRule::check(const ScheduleView& view, std::vector<Violation>& out) const {
    for (const Resource& resource : view.scenario().resources) {
        const std::span<const PlacedStep> steps = view.on_resource(resource.id);
        const Ms settle = resource.constraints.settle_time_ms;
        for (std::size_t i = 1; i < steps.size(); ++i) {
            const PlacedStep& prev = steps[i - 1];
            const PlacedStep& cur = steps[i];
            const Ms gap = cur.when.start - prev.when.end;
            if (gap >= settle) continue;

            const std::string detail =
                gap < 0 ? std::format("같은 자원의 '{}'와(과) {}ms 겹칩니다.", prev.step->id, -gap)
                        : std::format("'{}' 종료 후 안정화 {}ms가 필요한데 간격이 {}ms뿐입니다.",
                                      prev.step->id, settle, gap);
            out.push_back(Violation{
                .code = ViolationCode::SettleViolation,
                .step_id = cur.step->id,
                .related_id = prev.step->id,
                .resource_id = resource.id,
                .detail = detail,
            });
        }
    }
}

void DutyRule::check(const ScheduleView& view, std::vector<Violation>& out) const {
    for (const Resource& resource : view.scenario().resources) {
        const Constraints& c = resource.constraints;
        if (c.max_continuous_ms <= 0) continue;  // 무제한

        const std::span<const PlacedStep> steps = view.on_resource(resource.id);
        Ms run_ms = 0;          // 현재 연속 구간에 쌓인 동작 시간
        Ms run_end = 0;         // 직전 동작의 종료 시각
        bool have_prev = false;
        bool limit_reached = false;

        for (const PlacedStep& cur : steps) {
            const Ms gap = have_prev ? cur.when.start - run_end : 0;

            if (!have_prev || gap >= c.cooldown_ms) {
                // 휴지를 충분히 가졌으므로 연속 구간이 끊긴 것으로 본다.
                run_ms = 0;
                limit_reached = false;
            } else if (limit_reached) {
                out.push_back(Violation{
                    .code = ViolationCode::CooldownViolation,
                    .step_id = cur.step->id,
                    .resource_id = resource.id,
                    .detail = std::format(
                        "연속 동작 한도({}ms) 도달 후 휴지 {}ms가 필요한데 {}ms 만에 다시 "
                        "동작합니다.",
                        c.max_continuous_ms, c.cooldown_ms, gap),
                });
                // 같은 원인으로 뒤따르는 동작까지 연쇄 보고되지 않도록 구간을 새로 연다.
                run_ms = 0;
                limit_reached = false;
            }

            run_ms += cur.when.length();
            if (run_ms > c.max_continuous_ms) {
                out.push_back(Violation{
                    .code = ViolationCode::DutyExceeded,
                    .step_id = cur.step->id,
                    .resource_id = resource.id,
                    .detail = std::format("연속 동작 {}ms가 한도 {}ms를 초과합니다.", run_ms,
                                          c.max_continuous_ms),
                });
            }
            if (run_ms >= c.max_continuous_ms) limit_reached = true;

            run_end = cur.when.end;
            have_prev = true;
        }
    }
}

void ExclusiveRule::check(const ScheduleView& view, std::vector<Violation>& out) const {
    // 배타 관계는 대칭이다. 한쪽만 선언해도 성립하며, 양쪽이 선언해도 한 번만 검사한다.
    std::set<std::pair<std::string, std::string>> pairs;
    for (const Resource& resource : view.scenario().resources) {
        for (const std::string& other : resource.constraints.exclusive_with) {
            if (other == resource.id) continue;
            pairs.emplace(std::min(resource.id, other), std::max(resource.id, other));
        }
    }

    for (const auto& [left, right] : pairs) {
        for (const PlacedStep& a : view.on_resource(left)) {
            for (const PlacedStep& b : view.on_resource(right)) {
                if (!a.when.overlaps(b.when)) continue;
                const Ms overlap = std::min(a.when.end, b.when.end) -
                                   std::max(a.when.start, b.when.start);
                out.push_back(Violation{
                    .code = ViolationCode::ExclusiveConflict,
                    .step_id = a.step->id,
                    .related_id = b.step->id,
                    .resource_id = left,
                    .detail = std::format(
                        "배타 자원 '{}'와(과) '{}'의 동작이 {}ms 겹칩니다 ('{}' vs '{}').", left,
                        right, overlap, a.step->id, b.step->id),
                });
            }
        }
    }
}

}  // namespace ess

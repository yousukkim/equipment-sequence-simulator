#include <doctest/doctest.h>

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

#include "model/types.hpp"
#include "validate/validator.hpp"

namespace {

using ess::Ms;
using ess::ViolationCode;

bool has_code(const std::vector<ess::Violation>& violations, ViolationCode code) {
    return std::any_of(violations.begin(), violations.end(),
                       [&](const ess::Violation& v) { return v.code == code; });
}

std::size_t count_code(const std::vector<ess::Violation>& violations, ViolationCode code) {
    return static_cast<std::size_t>(std::count_if(
        violations.begin(), violations.end(), [&](const ess::Violation& v) { return v.code == code; }));
}

const ess::Violation* find_code(const std::vector<ess::Violation>& violations, ViolationCode code) {
    const auto it = std::find_if(violations.begin(), violations.end(),
                                 [&](const ess::Violation& v) { return v.code == code; });
    return it == violations.end() ? nullptr : &*it;
}

std::vector<ess::Violation> run(const ess::Scenario& scenario) {
    const ess::ScheduleView view{scenario};
    return ess::validate(view);
}

// 자원 하나짜리 기본 시나리오. 각 테스트가 필요한 부분만 바꿔 쓴다.
ess::Scenario single_resource(ess::Constraints constraints, std::vector<ess::Step> steps) {
    return ess::Scenario{
        .name = "테스트",
        .resources = {ess::Resource{.id = "r",
                                    .name = "자원 R",
                                    .type = ess::ResourceType::Generic,
                                    .constraints = std::move(constraints)}},
        .steps = std::move(steps),
    };
}

}  // namespace

TEST_CASE("제약을 모두 지킨 배치에는 위반이 없다") {
    const ess::Scenario scenario = single_resource(
        ess::Constraints{.rate_min = 1.0, .rate_max = 10.0, .settle_time_ms = 100},
        {
            ess::Step{.id = "a", .resource_id = "r", .duration_ms = 500, .rate = 5.0, .start_ms = 0},
            ess::Step{.id = "b",
                      .resource_id = "r",
                      .duration_ms = 300,
                      .rate = 2.0,
                      .depends_on = {"a"},
                      .start_ms = 600},
        });
    CHECK(run(scenario).empty());
}

TEST_CASE("RATE_OUT_OF_RANGE — 허용 범위를 벗어난 동작률") {
    const ess::Scenario scenario = single_resource(
        ess::Constraints{.rate_min = 1.0, .rate_max = 10.0},
        {
            ess::Step{.id = "느림", .resource_id = "r", .duration_ms = 100, .rate = 0.5, .start_ms = 0},
            ess::Step{.id = "정상", .resource_id = "r", .duration_ms = 100, .rate = 5.0, .start_ms = 200},
            ess::Step{.id = "빠름", .resource_id = "r", .duration_ms = 100, .rate = 50.0, .start_ms = 400},
        });
    const auto violations = run(scenario);
    CHECK(count_code(violations, ViolationCode::RateOutOfRange) == 2);
}

TEST_CASE("SETTLE_VIOLATION — 안정화 대기 부족과 구간 겹침") {
    SUBCASE("안정화 시간을 못 채운 경우") {
        const ess::Scenario scenario = single_resource(
            ess::Constraints{.settle_time_ms = 200},
            {
                ess::Step{.id = "a", .resource_id = "r", .duration_ms = 100, .start_ms = 0},
                ess::Step{.id = "b", .resource_id = "r", .duration_ms = 100, .start_ms = 150},
            });
        const auto violations = run(scenario);
        REQUIRE(count_code(violations, ViolationCode::SettleViolation) == 1);
        const ess::Violation* v = find_code(violations, ViolationCode::SettleViolation);
        CHECK(v->step_id == "b");
        CHECK(v->related_id == "a");
    }

    SUBCASE("안정화 시간이 0이어도 같은 자원에서 겹치면 위반이다") {
        const ess::Scenario scenario = single_resource(
            ess::Constraints{},
            {
                ess::Step{.id = "a", .resource_id = "r", .duration_ms = 500, .start_ms = 0},
                ess::Step{.id = "b", .resource_id = "r", .duration_ms = 500, .start_ms = 300},
            });
        CHECK(count_code(run(scenario), ViolationCode::SettleViolation) == 1);
    }

    SUBCASE("정확히 안정화 시간만큼 벌어지면 통과한다") {
        const ess::Scenario scenario = single_resource(
            ess::Constraints{.settle_time_ms = 200},
            {
                ess::Step{.id = "a", .resource_id = "r", .duration_ms = 100, .start_ms = 0},
                ess::Step{.id = "b", .resource_id = "r", .duration_ms = 100, .start_ms = 300},
            });
        CHECK(run(scenario).empty());
    }
}

TEST_CASE("PRECEDENCE_VIOLATION — 선행 동작이 끝나기 전에 시작") {
    const ess::Scenario scenario = single_resource(
        ess::Constraints{},
        {
            ess::Step{.id = "a", .resource_id = "r", .duration_ms = 500, .start_ms = 1000},
            ess::Step{.id = "b",
                      .resource_id = "r",
                      .duration_ms = 100,
                      .depends_on = {"a"},
                      .start_ms = 0},
        });
    const auto violations = run(scenario);
    REQUIRE(count_code(violations, ViolationCode::PrecedenceViolation) == 1);
    const ess::Violation* v = find_code(violations, ViolationCode::PrecedenceViolation);
    CHECK(v->step_id == "b");
    CHECK(v->related_id == "a");
}

TEST_CASE("CYCLIC_DEPENDENCY — 순환 의존은 배치와 무관하게 위반이다") {
    const ess::Scenario scenario = single_resource(
        ess::Constraints{},
        {
            ess::Step{.id = "a",
                      .resource_id = "r",
                      .duration_ms = 100,
                      .depends_on = {"c"},
                      .start_ms = 0},
            ess::Step{.id = "b",
                      .resource_id = "r",
                      .duration_ms = 100,
                      .depends_on = {"a"},
                      .start_ms = 200},
            ess::Step{.id = "c",
                      .resource_id = "r",
                      .duration_ms = 100,
                      .depends_on = {"b"},
                      .start_ms = 400},
        });
    const auto violations = run(scenario);
    // 순환은 한 번만 보고한다. 참여 동작마다 중복해서 내지 않는다.
    REQUIRE(count_code(violations, ViolationCode::CyclicDependency) == 1);
    const ess::Violation* v = find_code(violations, ViolationCode::CyclicDependency);
    CHECK(v->step_id == "a");  // 가장 작은 id에서 시작하도록 회전시킨다
    CHECK(v->detail.find("a → b → c → a") != std::string::npos);
}

TEST_CASE("DUTY_EXCEEDED — 연속 동작 한도 초과") {
    const ess::Constraints duty{.max_continuous_ms = 1000, .cooldown_ms = 500};

    SUBCASE("짧은 간격으로 이어 붙이면 누적되어 한도를 넘는다") {
        const ess::Scenario scenario = single_resource(
            duty,
            {
                ess::Step{.id = "a", .resource_id = "r", .duration_ms = 600, .start_ms = 0},
                ess::Step{.id = "b", .resource_id = "r", .duration_ms = 600, .start_ms = 700},
            });
        const auto violations = run(scenario);
        REQUIRE(count_code(violations, ViolationCode::DutyExceeded) == 1);
        CHECK(find_code(violations, ViolationCode::DutyExceeded)->step_id == "b");
    }

    SUBCASE("휴지를 충분히 두면 연속 구간이 끊겨 위반이 없다") {
        const ess::Scenario scenario = single_resource(
            duty,
            {
                ess::Step{.id = "a", .resource_id = "r", .duration_ms = 600, .start_ms = 0},
                ess::Step{.id = "b", .resource_id = "r", .duration_ms = 600, .start_ms = 1100},
            });
        CHECK(run(scenario).empty());
    }

    SUBCASE("한 동작만으로 한도를 넘겨도 잡힌다") {
        const ess::Scenario scenario = single_resource(
            duty, {ess::Step{.id = "긴동작", .resource_id = "r", .duration_ms = 1500, .start_ms = 0}});
        CHECK(count_code(run(scenario), ViolationCode::DutyExceeded) == 1);
    }
}

TEST_CASE("COOLDOWN_VIOLATION — 한도 도달 후 휴지를 못 채우고 재동작") {
    const ess::Scenario scenario = single_resource(
        ess::Constraints{.max_continuous_ms = 1000, .cooldown_ms = 500},
        {
            // a 하나로 한도에 정확히 도달한다. 이후에는 500ms 휴지가 필요하다.
            ess::Step{.id = "a", .resource_id = "r", .duration_ms = 1000, .start_ms = 0},
            ess::Step{.id = "b", .resource_id = "r", .duration_ms = 100, .start_ms = 1200},
        });
    const auto violations = run(scenario);
    REQUIRE(count_code(violations, ViolationCode::CooldownViolation) == 1);
    CHECK(find_code(violations, ViolationCode::CooldownViolation)->step_id == "b");
}

TEST_CASE("EXCLUSIVE_CONFLICT — 상호 배타 자원의 동작이 겹침") {
    const ess::Scenario scenario{
        .name = "배타",
        .resources =
            {
                ess::Resource{.id = "a", .constraints = {.exclusive_with = {"b"}}},
                ess::Resource{.id = "b"},  // 한쪽만 선언해도 대칭으로 본다
            },
        .steps =
            {
                ess::Step{.id = "sa", .resource_id = "a", .duration_ms = 500, .start_ms = 0},
                ess::Step{.id = "sb", .resource_id = "b", .duration_ms = 500, .start_ms = 300},
                ess::Step{.id = "sc", .resource_id = "b", .duration_ms = 100, .start_ms = 900},
            },
    };
    const auto violations = run(scenario);
    REQUIRE(count_code(violations, ViolationCode::ExclusiveConflict) == 1);
    const ess::Violation* v = find_code(violations, ViolationCode::ExclusiveConflict);
    CHECK(v->step_id == "sa");
    CHECK(v->related_id == "sb");
}

TEST_CASE("양쪽 모두 배타를 선언해도 한 번만 보고한다") {
    const ess::Scenario scenario{
        .name = "배타 대칭",
        .resources =
            {
                ess::Resource{.id = "a", .constraints = {.exclusive_with = {"b"}}},
                ess::Resource{.id = "b", .constraints = {.exclusive_with = {"a"}}},
            },
        .steps =
            {
                ess::Step{.id = "sa", .resource_id = "a", .duration_ms = 500, .start_ms = 0},
                ess::Step{.id = "sb", .resource_id = "b", .duration_ms = 500, .start_ms = 100},
            },
    };
    CHECK(count_code(run(scenario), ViolationCode::ExclusiveConflict) == 1);
}

TEST_CASE("입력 순서가 달라도 결과가 같다") {
    // PlacedStep의 <=>가 (구간, id) 순서를 고정하므로 보고 순서가 흔들리지 않아야 한다.
    const ess::Constraints c{.rate_min = 1.0, .rate_max = 10.0, .settle_time_ms = 200};
    std::vector<ess::Step> steps{
        ess::Step{.id = "s1", .resource_id = "r", .duration_ms = 300, .rate = 0.5, .start_ms = 0},
        ess::Step{.id = "s2", .resource_id = "r", .duration_ms = 300, .rate = 5.0, .start_ms = 400},
        ess::Step{.id = "s3", .resource_id = "r", .duration_ms = 300, .rate = 99.0, .start_ms = 900},
    };
    const auto forward = run(single_resource(c, steps));
    std::reverse(steps.begin(), steps.end());
    const auto reversed = run(single_resource(c, steps));

    REQUIRE(forward.size() == reversed.size());
    REQUIRE_FALSE(forward.empty());
    for (std::size_t i = 0; i < forward.size(); ++i) {
        CHECK(forward[i].code == reversed[i].code);
        CHECK(forward[i].step_id == reversed[i].step_id);
        CHECK(forward[i].detail == reversed[i].detail);
    }
}

TEST_CASE("시각이 배치되지 않은 동작을 먼저 걸러낸다") {
    const ess::Scenario scenario = single_resource(
        ess::Constraints{},
        {
            ess::Step{.id = "a", .resource_id = "r", .duration_ms = 100, .start_ms = 0},
            ess::Step{.id = "b", .resource_id = "r", .duration_ms = 100},  // start_ms 없음
        });
    const auto unplaced = ess::ScheduleView::unplaced_steps(scenario);
    REQUIRE(unplaced.size() == 1);
    CHECK(unplaced[0] == "b");
}

TEST_CASE("규칙 목록이 위반 코드 7종을 모두 덮는다") {
    std::vector<ViolationCode> covered;
    for (const ess::RuleInfo& rule : ess::rule_catalog()) {
        for (const ViolationCode code : rule.codes) covered.push_back(code);
    }
    const ViolationCode all[] = {
        ViolationCode::RateOutOfRange,     ViolationCode::SettleViolation,
        ViolationCode::ExclusiveConflict,  ViolationCode::DutyExceeded,
        ViolationCode::CooldownViolation,  ViolationCode::PrecedenceViolation,
        ViolationCode::CyclicDependency,
    };
    for (const ViolationCode code : all) {
        CHECK(std::find(covered.begin(), covered.end(), code) != covered.end());
    }
    CHECK(covered.size() == std::size(all));
}

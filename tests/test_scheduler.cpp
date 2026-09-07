#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "io/scenario_loader.hpp"
#include "io/scenario_writer.hpp"
#include "model/dependency_graph.hpp"
#include "model/types.hpp"
#include "schedule/scheduler.hpp"
#include "validate/validator.hpp"

namespace {

using ess::Ms;
using ess::ViolationCode;

// 스케줄러의 출력을 그대로 검증기에 넣는다. M2의 핵심 성질이다.
std::vector<ess::Violation> validate_result(const ess::Scenario& scenario) {
    const ess::ScheduleView view{scenario};
    return ess::validate(view);
}

Ms start_of(const ess::Scenario& scenario, std::string_view id) {
    for (const ess::Step& step : scenario.steps) {
        if (step.id == id) return step.start_ms.value_or(-1);
    }
    return -1;
}

Ms makespan(const ess::Scenario& scenario) {
    Ms end = 0;
    for (const ess::Step& step : scenario.steps) {
        end = std::max(end, step.start_ms.value_or(0) + step.duration_ms);
    }
    return end;
}

bool has_code(const std::vector<ess::Violation>& items, ViolationCode code) {
    return std::any_of(items.begin(), items.end(),
                       [&](const ess::Violation& v) { return v.code == code; });
}

}  // namespace

TEST_CASE("위상 정렬은 선행 관계를 지키고 입력 순서를 우선한다") {
    const ess::Scenario scenario{
        .name = "위상",
        .resources = {ess::Resource{.id = "r"}},
        .steps =
            {
                ess::Step{.id = "c", .resource_id = "r", .duration_ms = 100, .depends_on = {"a"}},
                ess::Step{.id = "a", .resource_id = "r", .duration_ms = 100},
                ess::Step{.id = "b", .resource_id = "r", .duration_ms = 100},
            },
    };
    const auto order = ess::topological_order(scenario);
    REQUIRE(order.has_value());
    REQUIRE(order->size() == 3);
    // 매 순간 "지금 놓을 수 있는 동작 중 입력에서 가장 먼저 쓴 것"을 고른다.
    //  - 처음에 놓을 수 있는 건 a(1)와 b(2). 인덱스가 작은 a가 먼저.
    //  - a를 놓으면 c(0)가 풀린다. 이제 c(0)와 b(2) 중 c가 앞선다.
    // 입력 파일에 쓴 순서가 곧 우선순위라는 뜻이다.
    CHECK((*order)[0] == 1);  // a
    CHECK((*order)[1] == 0);  // c
    CHECK((*order)[2] == 2);  // b
}

TEST_CASE("순환이 있으면 위상 정렬이 실패한다") {
    const ess::Scenario scenario{
        .name = "순환",
        .resources = {ess::Resource{.id = "r"}},
        .steps =
            {
                ess::Step{.id = "a", .resource_id = "r", .duration_ms = 100, .depends_on = {"b"}},
                ess::Step{.id = "b", .resource_id = "r", .duration_ms = 100, .depends_on = {"a"}},
            },
    };
    CHECK_FALSE(ess::topological_order(scenario).has_value());

    const ess::ScheduleResult result = ess::schedule(scenario);
    CHECK_FALSE(result.ok());
    CHECK(has_code(result.failures, ViolationCode::CyclicDependency));
}

TEST_CASE("선행 관계를 지켜 배치한다") {
    const ess::Scenario scenario{
        .name = "선행",
        .resources = {ess::Resource{.id = "r1"}, ess::Resource{.id = "r2"}},
        .steps =
            {
                ess::Step{.id = "b",
                          .resource_id = "r2",
                          .duration_ms = 300,
                          .depends_on = {"a"}},
                ess::Step{.id = "a", .resource_id = "r1", .duration_ms = 500},
            },
    };
    const ess::ScheduleResult result = ess::schedule(scenario);
    REQUIRE(result.ok());
    CHECK(start_of(result.scenario, "a") == 0);
    CHECK(start_of(result.scenario, "b") == 500);
    CHECK(validate_result(result.scenario).empty());
}

TEST_CASE("같은 자원에서 안정화 시간을 확보한다") {
    const ess::Scenario scenario{
        .name = "안정화",
        .resources = {ess::Resource{.id = "r", .constraints = {.settle_time_ms = 200}}},
        .steps =
            {
                ess::Step{.id = "a", .resource_id = "r", .duration_ms = 500},
                ess::Step{.id = "b", .resource_id = "r", .duration_ms = 500},
            },
    };
    const ess::ScheduleResult result = ess::schedule(scenario);
    REQUIRE(result.ok());
    CHECK(start_of(result.scenario, "a") == 0);
    CHECK(start_of(result.scenario, "b") == 700);  // 500 종료 + 안정화 200
    CHECK(validate_result(result.scenario).empty());
}

TEST_CASE("연속 동작 한도에 걸리면 휴지를 넣는다") {
    const ess::Scenario scenario{
        .name = "연속 한도",
        .resources = {ess::Resource{.id = "r",
                                    .constraints = {.max_continuous_ms = 1000,
                                                    .cooldown_ms = 500}}},
        .steps =
            {
                ess::Step{.id = "a", .resource_id = "r", .duration_ms = 600},
                ess::Step{.id = "b", .resource_id = "r", .duration_ms = 600},
            },
    };
    const ess::ScheduleResult result = ess::schedule(scenario);
    REQUIRE(result.ok());
    CHECK(start_of(result.scenario, "a") == 0);
    // 이어 붙이면 1200ms로 한도를 넘으므로 600 종료 후 휴지 500을 확보한다.
    CHECK(start_of(result.scenario, "b") == 1100);
    CHECK(validate_result(result.scenario).empty());
}

TEST_CASE("배타 자원의 동작을 겹치지 않게 밀어낸다") {
    const ess::Scenario scenario{
        .name = "배타 회피",
        .resources =
            {
                ess::Resource{.id = "a", .constraints = {.exclusive_with = {"b"}}},
                ess::Resource{.id = "b"},
            },
        .steps =
            {
                ess::Step{.id = "sa", .resource_id = "a", .duration_ms = 500},
                ess::Step{.id = "sb", .resource_id = "b", .duration_ms = 500},
            },
    };
    const ess::ScheduleResult result = ess::schedule(scenario);
    REQUIRE(result.ok());
    CHECK(start_of(result.scenario, "sa") == 0);
    CHECK(start_of(result.scenario, "sb") == 500);  // 다른 자원이지만 배타라 뒤로 밀린다
    CHECK(validate_result(result.scenario).empty());
}

TEST_CASE("배치를 바꿔도 고칠 수 없는 제약은 배치 전에 보고한다") {
    SUBCASE("동작률이 범위를 벗어남") {
        const ess::Scenario scenario{
            .name = "동작률",
            .resources = {ess::Resource{.id = "r",
                                        .constraints = {.rate_min = 1.0, .rate_max = 10.0}}},
            .steps = {ess::Step{.id = "a", .resource_id = "r", .duration_ms = 100, .rate = 50.0}},
        };
        const ess::ScheduleResult result = ess::schedule(scenario);
        CHECK_FALSE(result.ok());
        CHECK(has_code(result.failures, ViolationCode::RateOutOfRange));
    }

    SUBCASE("동작 하나가 연속 동작 한도보다 김") {
        const ess::Scenario scenario{
            .name = "한도 초과",
            .resources = {ess::Resource{.id = "r",
                                        .constraints = {.max_continuous_ms = 1000,
                                                        .cooldown_ms = 500}}},
            .steps = {ess::Step{.id = "긴동작", .resource_id = "r", .duration_ms = 1500}},
        };
        const ess::ScheduleResult result = ess::schedule(scenario);
        CHECK_FALSE(result.ok());
        CHECK(has_code(result.failures, ViolationCode::DutyExceeded));
    }
}

TEST_CASE("입력에 있던 시각은 무시하고 새로 배치한다") {
    const ess::Scenario scenario{
        .name = "재배치",
        .resources = {ess::Resource{.id = "r", .constraints = {.settle_time_ms = 100}}},
        .steps =
            {
                ess::Step{.id = "a", .resource_id = "r", .duration_ms = 100, .start_ms = 9999},
                ess::Step{.id = "b", .resource_id = "r", .duration_ms = 100, .start_ms = 5},
            },
    };
    const ess::ScheduleResult result = ess::schedule(scenario);
    REQUIRE(result.ok());
    CHECK(start_of(result.scenario, "a") == 0);
    CHECK(start_of(result.scenario, "b") == 200);
    CHECK(validate_result(result.scenario).empty());
}

TEST_CASE("같은 입력이면 같은 결과가 나온다") {
    const ess::Scenario scenario{
        .name = "결정론",
        .resources =
            {
                ess::Resource{.id = "a",
                              .constraints = {.settle_time_ms = 100, .exclusive_with = {"b"}}},
                ess::Resource{.id = "b", .constraints = {.settle_time_ms = 50}},
            },
        .steps =
            {
                ess::Step{.id = "s1", .resource_id = "a", .duration_ms = 300},
                ess::Step{.id = "s2", .resource_id = "b", .duration_ms = 200},
                ess::Step{.id = "s3", .resource_id = "a", .duration_ms = 400, .depends_on = {"s2"}},
                ess::Step{.id = "s4", .resource_id = "b", .duration_ms = 100, .depends_on = {"s1"}},
            },
    };
    const ess::ScheduleResult first = ess::schedule(scenario);
    const ess::ScheduleResult second = ess::schedule(scenario);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    for (std::size_t i = 0; i < first.scenario.steps.size(); ++i) {
        CHECK(first.scenario.steps[i].start_ms == second.scenario.steps[i].start_ms);
    }
    CHECK(validate_result(first.scenario).empty());
}

TEST_CASE("예제 시나리오를 스케줄하면 검증을 통과한다") {
    // 여러 제약이 한꺼번에 걸린 입력으로 왕복(스케줄 → 검증)을 확인한다.
    const char* text = R"json({
      "name": "왕복",
      "resources": [
        { "id": "rot", "type": "Rotary",
          "constraints": { "rate_min": 1.0, "rate_max": 12.0, "settle_time_ms": 200,
                           "max_continuous_ms": 2000, "cooldown_ms": 1000 } },
        { "id": "emit", "type": "Emitter",
          "constraints": { "rate_min": 0.5, "rate_max": 5.0, "settle_time_ms": 300,
                           "exclusive_with": ["rot"] } },
        { "id": "sens", "type": "Sensor", "constraints": { "settle_time_ms": 50 } }
      ],
      "steps": [
        { "id": "spin_1",  "resource_id": "rot",  "duration_ms": 1200, "rate": 6.0 },
        { "id": "spin_2",  "resource_id": "rot",  "duration_ms": 1200, "rate": 6.0 },
        { "id": "emit_1",  "resource_id": "emit", "duration_ms": 900,  "rate": 2.0,
          "depends_on": ["spin_1"] },
        { "id": "read_1",  "resource_id": "sens", "duration_ms": 900,  "rate": 0.0,
          "depends_on": ["spin_1"] },
        { "id": "emit_2",  "resource_id": "emit", "duration_ms": 900,  "rate": 2.0,
          "depends_on": ["spin_2", "emit_1"] }
      ]
    })json";

    const ess::LoadResult loaded = ess::load_scenario_text(text);
    REQUIRE(loaded.ok());

    const ess::ScheduleResult result = ess::schedule(loaded.scenario);
    REQUIRE(result.ok());

    // 모든 동작에 시각이 채워졌는가
    CHECK(ess::ScheduleView::unplaced_steps(result.scenario).empty());
    // 그리고 그 배치가 검증기를 통과하는가 — 이것이 M2의 완료 조건이다
    CHECK(validate_result(result.scenario).empty());
    CHECK(makespan(result.scenario) > 0);
}

TEST_CASE("배치 결과를 JSON으로 쓰고 다시 읽어도 검증을 통과한다") {
    const ess::Scenario scenario{
        .name = "왕복 직렬화",
        .resources =
            {
                ess::Resource{.id = "rot",
                              .name = "회전축",
                              .type = ess::ResourceType::Rotary,
                              .constraints = {.rate_min = 1.0,
                                              .rate_max = 10.0,
                                              .settle_time_ms = 200,
                                              .exclusive_with = {"emit"}}},
                ess::Resource{.id = "emit", .type = ess::ResourceType::Emitter},
            },
        .steps =
            {
                ess::Step{.id = "a", .resource_id = "rot", .duration_ms = 500, .rate = 5.0},
                ess::Step{.id = "b",
                          .resource_id = "emit",
                          .duration_ms = 300,
                          .rate = 0.0,
                          .depends_on = {"a"}},
            },
    };
    const ess::ScheduleResult scheduled = ess::schedule(scenario);
    REQUIRE(scheduled.ok());

    const std::string text = ess::to_json_text(scheduled.scenario);
    const ess::LoadResult reloaded = ess::load_scenario_text(text);
    REQUIRE(reloaded.ok());

    // 시각까지 그대로 복원되어야 한다
    CHECK(start_of(reloaded.scenario, "a") == start_of(scheduled.scenario, "a"));
    CHECK(start_of(reloaded.scenario, "b") == start_of(scheduled.scenario, "b"));
    CHECK(validate_result(reloaded.scenario).empty());
}

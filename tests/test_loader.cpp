#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "io/scenario_loader.hpp"

namespace {

bool has_error_at(const std::vector<ess::LoadError>& errors, std::string_view where) {
    return std::any_of(errors.begin(), errors.end(),
                       [&](const ess::LoadError& e) { return e.where == where; });
}

constexpr const char* kValid = R"json({
  "name": "샘플",
  "resources": [
    {
      "id": "rot_a",
      "name": "Rotator A",
      "type": "Rotary",
      "constraints": {
        "rate_min": 1.0,
        "rate_max": 10.0,
        "settle_time_ms": 200,
        "exclusive_with": ["lin_b"]
      }
    },
    { "id": "lin_b", "type": "Linear" }
  ],
  "steps": [
    { "id": "s1", "resource_id": "rot_a", "duration_ms": 500, "rate": 5.0, "start_ms": 0 },
    { "id": "s2", "resource_id": "lin_b", "duration_ms": 300, "rate": 0.0,
      "depends_on": ["s1"] }
  ]
})json";

}  // namespace

TEST_CASE("정상 시나리오를 읽는다") {
    const ess::LoadResult result = ess::load_scenario_text(kValid);
    REQUIRE(result.ok());

    const ess::Scenario& s = result.scenario;
    CHECK(s.name == "샘플");
    REQUIRE(s.resources.size() == 2);
    CHECK(s.resources[0].id == "rot_a");
    CHECK(s.resources[0].name == "Rotator A");
    CHECK(s.resources[0].type == ess::ResourceType::Rotary);
    CHECK(s.resources[0].constraints.rate_max == doctest::Approx(10.0));
    CHECK(s.resources[0].constraints.settle_time_ms == 200);
    REQUIRE(s.resources[0].constraints.exclusive_with.size() == 1);
    CHECK(s.resources[0].constraints.exclusive_with[0] == "lin_b");

    // name을 생략하면 id를 그대로 쓴다.
    CHECK(s.resources[1].name == "lin_b");
    // 제약을 생략하면 무제약이다.
    CHECK(std::isinf(s.resources[1].constraints.rate_max));

    REQUIRE(s.steps.size() == 2);
    REQUIRE(s.steps[0].start_ms.has_value());
    CHECK(*s.steps[0].start_ms == 0);
    // start_ms는 선택 항목이다. 비어 있으면 M2의 스케줄러가 채운다.
    CHECK_FALSE(s.steps[1].start_ms.has_value());
    REQUIRE(s.steps[1].depends_on.size() == 1);
    CHECK(s.steps[1].depends_on[0] == "s1");
}

TEST_CASE("JSON 구문 오류는 위치와 함께 보고한다") {
    const ess::LoadResult result = ess::load_scenario_text(R"({ "resources": [ )");
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.errors.size() == 1);
    CHECK(result.errors[0].message.find("JSON 구문 오류") != std::string::npos);
}

TEST_CASE("알 수 없는 자원을 참조하면 위치를 짚어 준다") {
    const ess::LoadResult result = ess::load_scenario_text(R"json({
      "resources": [ { "id": "rot_a" } ],
      "steps": [ { "id": "s1", "resource_id": "없는자원", "duration_ms": 100, "rate": 0 } ]
    })json");
    REQUIRE_FALSE(result.ok());
    CHECK(has_error_at(result.errors, "/steps/0/resource_id"));
}

TEST_CASE("알 수 없는 선행 동작과 자기 의존을 잡는다") {
    const ess::LoadResult result = ess::load_scenario_text(R"json({
      "resources": [ { "id": "r" } ],
      "steps": [
        { "id": "s1", "resource_id": "r", "duration_ms": 100, "rate": 0,
          "depends_on": ["s1"] },
        { "id": "s2", "resource_id": "r", "duration_ms": 100, "rate": 0,
          "depends_on": ["없는동작"] }
      ]
    })json");
    REQUIRE_FALSE(result.ok());
    CHECK(has_error_at(result.errors, "/steps/0/depends_on/0"));
    CHECK(has_error_at(result.errors, "/steps/1/depends_on/0"));
}

TEST_CASE("중복 id를 잡는다") {
    const ess::LoadResult result = ess::load_scenario_text(R"json({
      "resources": [ { "id": "r" }, { "id": "r" } ],
      "steps": [
        { "id": "s", "resource_id": "r", "duration_ms": 100, "rate": 0 },
        { "id": "s", "resource_id": "r", "duration_ms": 100, "rate": 0 }
      ]
    })json");
    REQUIRE_FALSE(result.ok());
    CHECK(has_error_at(result.errors, "/resources/1/id"));
    CHECK(has_error_at(result.errors, "/steps/1/id"));
}

TEST_CASE("한 번에 여러 오류를 모아서 돌려준다") {
    // 필수 항목 누락 + 음수 시간 + 잘못된 자원 종류를 한 파일에 담았다.
    const ess::LoadResult result = ess::load_scenario_text(R"json({
      "resources": [ { "id": "r", "type": "회전축" } ],
      "steps": [
        { "id": "s1", "resource_id": "r", "rate": 1.0 },
        { "id": "s2", "resource_id": "r", "duration_ms": 100 },
        { "id": "s3", "resource_id": "r", "duration_ms": 100, "rate": 1.0, "start_ms": -5 }
      ]
    })json");
    REQUIRE_FALSE(result.ok());
    CHECK(has_error_at(result.errors, "/resources/0/type"));
    CHECK(has_error_at(result.errors, "/steps/0/duration_ms"));
    CHECK(has_error_at(result.errors, "/steps/1/rate"));
    CHECK(has_error_at(result.errors, "/steps/2/start_ms"));
    CHECK(result.errors.size() >= 4);
}

TEST_CASE("rate_min이 rate_max보다 크면 오류다") {
    const ess::LoadResult result = ess::load_scenario_text(R"json({
      "resources": [
        { "id": "r", "constraints": { "rate_min": 10.0, "rate_max": 1.0 } }
      ],
      "steps": []
    })json");
    REQUIRE_FALSE(result.ok());
    CHECK(has_error_at(result.errors, "/resources/0/constraints"));
}

TEST_CASE("max_continuous_ms만 있고 cooldown_ms가 없으면 오류다") {
    // cooldown_ms는 휴지 시간이자 연속 동작이 끊겼다고 볼 최소 간격이다.
    // 0이면 연속 동작 한도가 조용히 무의미해지므로 입력 단계에서 막는다.
    const ess::LoadResult result = ess::load_scenario_text(R"json({
      "resources": [
        { "id": "r", "constraints": { "max_continuous_ms": 1000 } }
      ],
      "steps": []
    })json");
    REQUIRE_FALSE(result.ok());
    CHECK(has_error_at(result.errors, "/resources/0/constraints"));
}

TEST_CASE("배타 관계가 자기 자신이거나 없는 자원이면 오류다") {
    const ess::LoadResult result = ess::load_scenario_text(R"json({
      "resources": [
        { "id": "a", "constraints": { "exclusive_with": ["a", "없음"] } }
      ],
      "steps": []
    })json");
    REQUIRE_FALSE(result.ok());
    CHECK(has_error_at(result.errors, "/resources/0/constraints/exclusive_with/0"));
    CHECK(has_error_at(result.errors, "/resources/0/constraints/exclusive_with/1"));
}

TEST_CASE("resources나 steps가 없으면 오류다") {
    const ess::LoadResult result = ess::load_scenario_text("{}");
    REQUIRE_FALSE(result.ok());
    CHECK(has_error_at(result.errors, "/resources"));
    CHECK(has_error_at(result.errors, "/steps"));
}

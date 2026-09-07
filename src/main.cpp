#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <algorithm>
#include <cstddef>
#include <format>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "io/scenario_loader.hpp"
#include "io/scenario_writer.hpp"
#include "schedule/scheduler.hpp"
#include "validate/validator.hpp"

namespace {

// 종료 코드. 스크립트나 CI에서 결과를 구분할 수 있어야 한다.
constexpr int kOk = 0;
constexpr int kViolationsFound = 1;  // schedule에서는 "스케줄 불가"
constexpr int kInputError = 2;
constexpr int kInternalError = 3;  // 스케줄 결과가 자체 검증을 통과하지 못함 = 버그

void enable_utf8_console() {
#ifdef _WIN32
    ::SetConsoleOutputCP(CP_UTF8);
#endif
}

void print_usage() {
    std::cout << "장비 동작 시퀀스 시뮬레이터 (ess)\n"
                 "\n"
                 "사용법:\n"
                 "  ess validate <시나리오.json>            시각이 배치된 시나리오의 제약 위반을 검사\n"
                 "  ess schedule <시나리오.json> [-o <파일>]  제약을 만족하는 시각을 배치\n"
                 "  ess rules                               검증 규칙과 위반 코드 목록\n"
                 "  ess --help                              이 도움말\n"
                 "\n"
                 "종료 코드: 0 정상 / 1 위반 또는 스케줄 불가 / 2 입력 오류 / 3 내부 오류\n";
}

int run_rules() {
    std::cout << "검증 규칙 목록\n\n";
    for (const ess::RuleInfo& rule : ess::rule_catalog()) {
        std::cout << std::format("  {:<12}", rule.name);
        bool first = true;
        for (const ess::ViolationCode code : rule.codes) {
            std::cout << (first ? "" : ", ") << ess::to_string(code);
            first = false;
        }
        std::cout << '\n';
    }
    return kOk;
}

// 검증 위반과 스케줄 실패를 같은 형식으로 출력한다. 둘 다 Violation을 쓴다.
void print_violations(const std::vector<ess::Violation>& items) {
    for (const ess::Violation& v : items) {
        std::cout << std::format("[{}] 동작 '{}'", ess::to_string(v.code), v.step_id);
        if (!v.resource_id.empty()) std::cout << std::format(" (자원 '{}')", v.resource_id);
        std::cout << '\n' << "  " << v.detail << '\n';
    }
}

int run_validate(std::string_view path) {
    const ess::LoadResult loaded = ess::load_scenario_file(path);
    if (!loaded.ok()) {
        std::cerr << std::format("입력 오류 {}건 — {}\n", loaded.errors.size(), path)
                  << ess::format_errors(loaded.errors);
        return kInputError;
    }

    const ess::Scenario& scenario = loaded.scenario;
    if (const auto unplaced = ess::ScheduleView::unplaced_steps(scenario); !unplaced.empty()) {
        std::cerr << std::format(
            "validate는 시각이 배치된 시나리오를 검사합니다. start_ms가 없는 동작 {}건:\n",
            unplaced.size());
        for (const std::string& id : unplaced) std::cerr << "  " << id << '\n';
        std::cerr << "(시각을 자동으로 배치하는 기능은 M2의 schedule 명령에서 제공합니다.)\n";
        return kInputError;
    }

    const ess::ScheduleView view{scenario};
    const std::vector<ess::Violation> violations = ess::validate(view);

    const std::string title = scenario.name.empty() ? std::string{path} : scenario.name;
    std::cout << std::format("시나리오: {} (자원 {}, 동작 {})\n", title, scenario.resources.size(),
                             scenario.steps.size());

    if (violations.empty()) {
        std::cout << "위반 없음.\n";
        return kOk;
    }

    std::cout << std::format("\n위반 {}건\n\n", violations.size());
    print_violations(violations);
    return kViolationsFound;
}

int run_schedule(std::string_view path, std::string_view out_path) {
    const ess::LoadResult loaded = ess::load_scenario_file(path);
    if (!loaded.ok()) {
        std::cerr << std::format("입력 오류 {}건 — {}\n", loaded.errors.size(), path)
                  << ess::format_errors(loaded.errors);
        return kInputError;
    }

    const ess::Scenario& input = loaded.scenario;
    const ess::ScheduleResult scheduled = ess::schedule(input);

    const std::string title = input.name.empty() ? std::string{path} : input.name;
    std::cout << std::format("시나리오: {} (자원 {}, 동작 {})\n", title, input.resources.size(),
                             input.steps.size());

    if (!scheduled.ok()) {
        std::cout << std::format("\n스케줄 불가 {}건\n\n", scheduled.failures.size());
        print_violations(scheduled.failures);
        return kViolationsFound;
    }

    // 배치 결과는 시작 시각 순으로 보여 준다. 같은 시각이면 id 순.
    std::vector<const ess::Step*> ordered;
    ordered.reserve(scheduled.scenario.steps.size());
    for (const ess::Step& step : scheduled.scenario.steps) ordered.push_back(&step);
    std::ranges::sort(ordered, [](const ess::Step* a, const ess::Step* b) {
        if (*a->start_ms != *b->start_ms) return *a->start_ms < *b->start_ms;
        return a->id < b->id;
    });

    ess::Ms makespan = 0;
    for (const ess::Step* step : ordered) {
        makespan = std::max(makespan, *step->start_ms + step->duration_ms);
    }

    std::cout << std::format("\n배치 결과 — 총 소요 {}ms\n\n", makespan);
    std::cout << std::format("  {:>8}  {:>8}  {:<14} {}\n", "시작", "종료", "자원", "동작");
    for (const ess::Step* step : ordered) {
        std::cout << std::format("  {:>8}  {:>8}  {:<14} {}\n", *step->start_ms,
                                 *step->start_ms + step->duration_ms, step->resource_id, step->id);
    }

    // 스케줄러가 만든 배치를 그대로 검증기에 다시 넣는다.
    // 두 구현이 어긋나면 여기서 잡힌다. 통과하는 것이 정상이므로 실패는 버그다.
    const ess::ScheduleView view{scheduled.scenario};
    if (const auto violations = ess::validate(view); !violations.empty()) {
        std::cerr << "\n자체 검증 실패 — 배치 결과가 검증기를 통과하지 못했습니다. 버그입니다.\n\n";
        print_violations(violations);
        return kInternalError;
    }
    std::cout << "\n자체 검증: 위반 없음\n";

    if (!out_path.empty()) {
        std::ofstream out{std::string{out_path}, std::ios::binary};
        if (!out) {
            std::cerr << std::format("출력 파일을 열 수 없습니다: {}\n", out_path);
            return kInputError;
        }
        out << ess::to_json_text(scheduled.scenario);
        std::cout << std::format("배치 결과를 저장했습니다: {}\n", out_path);
    }
    return kOk;
}

}  // namespace

int main(int argc, char** argv) {
    enable_utf8_console();

    // argv를 span으로 감싸 인덱스 접근에 크기 정보를 함께 들고 다닌다.
    const std::span<char* const> args{argv, static_cast<std::size_t>(argc)};
    if (args.size() < 2) {
        print_usage();
        return kInputError;
    }

    const std::string_view command = args[1];
    if (command == "--help" || command == "-h" || command == "help") {
        print_usage();
        return kOk;
    }
    if (command == "rules") {
        return run_rules();
    }
    if (command == "validate") {
        if (args.size() < 3) {
            std::cerr << "시나리오 파일 경로가 필요합니다: ess validate <시나리오.json>\n";
            return kInputError;
        }
        return run_validate(args[2]);
    }
    if (command == "schedule") {
        if (args.size() < 3) {
            std::cerr << "시나리오 파일 경로가 필요합니다: ess schedule <시나리오.json> [-o <파일>]\n";
            return kInputError;
        }
        std::string_view out_path;
        for (std::size_t i = 3; i < args.size(); ++i) {
            const std::string_view option = args[i];
            if (option == "-o" || option == "--out") {
                if (i + 1 >= args.size()) {
                    std::cerr << "-o 다음에 출력 파일 경로가 필요합니다.\n";
                    return kInputError;
                }
                out_path = args[++i];
            } else {
                std::cerr << std::format("알 수 없는 옵션 '{}'\n", option);
                return kInputError;
            }
        }
        return run_schedule(args[2], out_path);
    }

    std::cerr << std::format("알 수 없는 명령 '{}'\n\n", command);
    print_usage();
    return kInputError;
}

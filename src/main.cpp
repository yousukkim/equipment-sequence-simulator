#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <cstddef>
#include <format>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "io/scenario_loader.hpp"
#include "validate/validator.hpp"

namespace {

// 종료 코드. 스크립트나 CI에서 결과를 구분할 수 있어야 한다.
constexpr int kOk = 0;
constexpr int kViolationsFound = 1;
constexpr int kInputError = 2;

void enable_utf8_console() {
#ifdef _WIN32
    ::SetConsoleOutputCP(CP_UTF8);
#endif
}

void print_usage() {
    std::cout << "장비 동작 시퀀스 시뮬레이터 (ess)\n"
                 "\n"
                 "사용법:\n"
                 "  ess validate <시나리오.json>   시각이 배치된 시나리오의 제약 위반을 검사\n"
                 "  ess rules                      검증 규칙과 각 규칙이 내는 위반 코드 목록\n"
                 "  ess --help                     이 도움말\n"
                 "\n"
                 "종료 코드: 0 위반 없음 / 1 위반 발견 / 2 입력 오류\n";
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
    for (const ess::Violation& v : violations) {
        std::cout << std::format("[{}] 동작 '{}'", ess::to_string(v.code), v.step_id);
        if (!v.resource_id.empty()) std::cout << std::format(" (자원 '{}')", v.resource_id);
        std::cout << '\n' << "  " << v.detail << '\n';
    }
    return kViolationsFound;
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

    std::cerr << std::format("알 수 없는 명령 '{}'\n\n", command);
    print_usage();
    return kInputError;
}

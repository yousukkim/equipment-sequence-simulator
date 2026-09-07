#include "model/dependency_graph.hpp"

#include <algorithm>
#include <unordered_map>

namespace ess {
namespace {

std::unordered_map<std::string, std::size_t> index_by_id(const Scenario& scenario) {
    std::unordered_map<std::string, std::size_t> out;
    for (std::size_t i = 0; i < scenario.steps.size(); ++i) {
        out.emplace(scenario.steps[i].id, i);
    }
    return out;
}

// 선행 → 후행 인접 리스트. 알 수 없는 선행 id는 건너뛴다 (로더가 걸러 주는 경우).
std::vector<std::vector<std::size_t>> build_adjacency(const Scenario& scenario) {
    const auto index = index_by_id(scenario);
    std::vector<std::vector<std::size_t>> adjacency(scenario.steps.size());
    for (std::size_t i = 0; i < scenario.steps.size(); ++i) {
        for (const std::string& dep : scenario.steps[i].depends_on) {
            if (const auto it = index.find(dep); it != index.end()) {
                adjacency[it->second].push_back(i);
            }
        }
    }
    return adjacency;
}

// 3색 DFS. 현재 경로 위(회색)의 노드를 다시 만나면 그 지점부터가 순환이다.
class CycleFinder {
public:
    CycleFinder(const std::vector<std::vector<std::size_t>>& adjacency, const Scenario& scenario)
        : adjacency_(adjacency), scenario_(scenario), color_(adjacency.size(), kWhite) {}

    void run() {
        for (std::size_t i = 0; i < adjacency_.size(); ++i) {
            if (color_[i] == kWhite) visit(i);
        }
    }

    std::set<std::vector<std::string>> take_cycles() { return std::move(cycles_); }

private:
    static constexpr int kWhite = 0;  // 미방문
    static constexpr int kGray = 1;   // 현재 경로 위
    static constexpr int kBlack = 2;  // 탐색 완료

    const std::vector<std::vector<std::size_t>>& adjacency_;
    const Scenario& scenario_;
    std::vector<int> color_;
    std::vector<std::size_t> path_;
    std::set<std::vector<std::string>> cycles_;

    void visit(std::size_t node) {
        color_[node] = kGray;
        path_.push_back(node);
        for (const std::size_t next : adjacency_[node]) {
            if (color_[next] == kWhite) {
                visit(next);
            } else if (color_[next] == kGray) {
                record_cycle(next);
            }
        }
        path_.pop_back();
        color_[node] = kBlack;
    }

    void record_cycle(std::size_t entry) {
        const auto it = std::ranges::find(path_, entry);
        if (it == path_.end()) return;
        std::vector<std::string> cycle;
        for (auto p = it; p != path_.end(); ++p) {
            cycle.push_back(scenario_.steps[*p].id);
        }
        const auto smallest = std::ranges::min_element(cycle);
        std::rotate(cycle.begin(), smallest, cycle.end());
        cycles_.insert(std::move(cycle));
    }
};

}  // namespace

std::set<std::vector<std::string>> find_dependency_cycles(const Scenario& scenario) {
    const auto adjacency = build_adjacency(scenario);
    CycleFinder finder{adjacency, scenario};
    finder.run();
    return finder.take_cycles();
}

std::optional<std::vector<std::size_t>> topological_order(const Scenario& scenario) {
    const auto adjacency = build_adjacency(scenario);

    std::vector<std::size_t> indegree(scenario.steps.size(), 0);
    for (const auto& nexts : adjacency) {
        for (const std::size_t next : nexts) ++indegree[next];
    }

    // std::set을 준비 목록으로 쓰면 항상 인덱스가 가장 작은 동작이 먼저 나온다.
    // 동시에 놓을 수 있는 동작이 여럿일 때 입력 순서가 우선순위가 된다.
    std::set<std::size_t> ready;
    for (std::size_t i = 0; i < indegree.size(); ++i) {
        if (indegree[i] == 0) ready.insert(i);
    }

    std::vector<std::size_t> order;
    order.reserve(scenario.steps.size());
    while (!ready.empty()) {
        const std::size_t node = *ready.begin();
        ready.erase(ready.begin());
        order.push_back(node);
        for (const std::size_t next : adjacency[node]) {
            if (--indegree[next] == 0) ready.insert(next);
        }
    }

    if (order.size() != scenario.steps.size()) return std::nullopt;  // 순환이 있다
    return order;
}

}  // namespace ess

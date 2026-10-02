// 单元测试：NUMA 拓扑解析与节点归属（纯函数，无需 NUMA 硬件、无需 libnuma）
#include "common/thread/numa_topology.hpp"

#include <cstdio>
#include <vector>

namespace {
int g_failures = 0;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                                              \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

using eugraph::common::NumaTopology;
using eugraph::common::parseCpuList;

void test_parse_single() {
    CHECK(parseCpuList("5") == std::vector<int>{5});
}
void test_parse_range() {
    CHECK(parseCpuList("0-3") == (std::vector<int>{0, 1, 2, 3}));
}
void test_parse_mixed_with_spaces() {
    CHECK(parseCpuList(" 0-1, 4 ,8-9 ") == (std::vector<int>{0, 1, 4, 8, 9}));
}
void test_parse_empty_and_malformed() {
    CHECK(parseCpuList("").empty());
    CHECK(parseCpuList("abc").empty());
    CHECK(parseCpuList("0-").empty());
}

void test_node_lookup() {
    const auto topo = NumaTopology::fromCpuLists({{0, 1, 2, 3}, {4, 5, 6, 7}});
    CHECK(topo.nodeCount() == 2);
    CHECK(topo.nodeOfCpu(0) == 0);
    CHECK(topo.nodeOfCpu(3) == 0);
    CHECK(topo.nodeOfCpu(4) == 1);
    CHECK(topo.nodeOfCpu(99) == -1);
    CHECK(topo.cpusOfNode(1) == (std::vector<int>{4, 5, 6, 7}));
    CHECK(topo.cpusOfNode(9).empty());
}

void test_bind_unknown_node_fails_gracefully() {
    const auto topo = NumaTopology::fromCpuLists({{0, 1}});
    CHECK(topo.bindCurrentThreadToNode(7) == false); // 未知节点：不崩、返回 false
}

void test_discover_single_node_machine() {
    // 本机为单节点；若 sysfs 不可用则跳过（不算失败）
    auto topo = NumaTopology::discover();
    if (topo.has_value()) {
        CHECK(topo->nodeCount() >= 1);
        CHECK(!topo->cpusOfNode(0).empty());
    } else {
        std::printf("  (跳过：sysfs 无 NUMA 节点)\n");
    }
}

} // namespace

int main() {
    std::printf("NumaTopology 单元测试\n");
    test_parse_single();
    test_parse_range();
    test_parse_mixed_with_spaces();
    test_parse_empty_and_malformed();
    test_node_lookup();
    test_bind_unknown_node_fails_gracefully();
    test_discover_single_node_machine();
    if (g_failures == 0) {
        std::printf("  PASSED 7 组断言全部通过\n");
        return 0;
    }
    std::printf("  FAILED %d 处断言\n", g_failures);
    return 1;
}

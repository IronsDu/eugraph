#pragma once
/// NUMA topology discovery and thread binding WITHOUT libnuma (no new dependency).
///
/// Why: a query's storage read and the compute that consumes it should stay on one NUMA node, so
/// the data is first-touched and consumed on the same node (node-local L3, no cross-socket traffic).
/// See docs/storage/thread-bound-session-cursor-pool.md (§3).
///
/// Discovery uses sysfs (`/sys/devices/system/node/nodeN/cpulist`); binding uses
/// pthread_setaffinity_np with a CPU set built from that list. The parsing and node lookup are
/// pure functions, so they are unit-testable without any NUMA hardware.
#include <pthread.h>
#include <sched.h>

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace eugraph::common {

/// Parses a Linux cpulist ("0-3,8-11", "5", "0-1,4") into ascending CPU ids.
/// Malformed fragments are skipped; an empty list yields an empty result.
inline std::vector<int> parseCpuList(std::string_view list) {
    std::vector<int> cpus;
    size_t pos = 0;
    while (pos < list.size()) {
        size_t comma = list.find(',', pos);
        std::string_view item =
            list.substr(pos, comma == std::string_view::npos ? std::string_view::npos : comma - pos);
        // trim spaces
        while (!item.empty() && (item.front() == ' ' || item.front() == '\t'))
            item.remove_prefix(1);
        while (!item.empty() && (item.back() == ' ' || item.back() == '\t'))
            item.remove_suffix(1);
        if (!item.empty()) {
            const size_t dash = item.find('-');
            try {
                if (dash == std::string_view::npos) {
                    cpus.push_back(std::stoi(std::string(item)));
                } else {
                    const int lo = std::stoi(std::string(item.substr(0, dash)));
                    const int hi = std::stoi(std::string(item.substr(dash + 1)));
                    for (int cpu = lo; cpu <= hi && cpu - lo < 4096; ++cpu)
                        cpus.push_back(cpu);
                }
            } catch (const std::exception&) {
                // skip malformed fragment
            }
        }
        if (comma == std::string_view::npos)
            break;
        pos = comma + 1;
    }
    std::sort(cpus.begin(), cpus.end());
    cpus.erase(std::unique(cpus.begin(), cpus.end()), cpus.end());
    return cpus;
}

class NumaTopology {
public:
    /// Discovers nodes by reading <root>/nodeN/cpulist. Returns nullopt when sysfs has no nodes.
    static std::optional<NumaTopology> discover(const std::string& root = "/sys/devices/system/node") {
        NumaTopology topo;
        for (int node = 0; node < 1024; ++node) {
            const std::string path = root + "/node" + std::to_string(node) + "/cpulist";
            std::ifstream in(path);
            if (!in)
                break; // node numbering is contiguous from 0
            std::string text;
            std::getline(in, text);
            topo.cpus_by_node_.push_back(parseCpuList(text));
        }
        if (topo.cpus_by_node_.empty())
            return std::nullopt;
        return topo;
    }

    /// Builds a topology directly (tests, or when sysfs is unavailable).
    static NumaTopology fromCpuLists(std::vector<std::vector<int>> cpus_by_node) {
        return NumaTopology{std::move(cpus_by_node)};
    }

    size_t nodeCount() const {
        return cpus_by_node_.size();
    }
    const std::vector<int>& cpusOfNode(size_t node) const {
        static const std::vector<int> empty;
        return node < cpus_by_node_.size() ? cpus_by_node_[node] : empty;
    }

    /// Node owning `cpu`, or -1 when unknown.
    int nodeOfCpu(int cpu) const {
        for (size_t node = 0; node < cpus_by_node_.size(); ++node) {
            const auto& cpus = cpus_by_node_[node];
            if (std::find(cpus.begin(), cpus.end(), cpu) != cpus.end())
                return static_cast<int>(node);
        }
        return -1;
    }

    /// Pins the CALLING thread to `node`'s CPUs. Node granularity on purpose: the node's cores stay
    /// interchangeable for load balancing, while node-local L3 and memory are preserved.
    /// Returns false when the node is unknown or the affinity call fails.
    bool bindCurrentThreadToNode(size_t node) const {
        const auto& cpus = cpusOfNode(node);
        if (cpus.empty())
            return false;
        cpu_set_t set;
        CPU_ZERO(&set);
        for (int cpu : cpus) {
            if (cpu >= 0 && cpu < CPU_SETSIZE)
                CPU_SET(cpu, &set);
        }
        return pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0;
    }

private:
    explicit NumaTopology(std::vector<std::vector<int>> cpus_by_node) : cpus_by_node_(std::move(cpus_by_node)) {}
    NumaTopology() = default;

    std::vector<std::vector<int>> cpus_by_node_;
};

} // namespace eugraph::common

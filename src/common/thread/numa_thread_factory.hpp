#pragma once
/// A folly thread factory that pins each created thread to a NUMA node (round-robin), so that IO
/// threads and CPU threads stay node-local. No-op unless EUGRAPH_NUMA_BIND=1, and a no-op on
/// single-node machines, so default behaviour is unchanged.
///
/// Rationale: keeping a query's storage read and the compute consuming it on the same node means
/// the produced chunk is first-touched and consumed locally (node-local L3, no cross-socket
/// traffic). Node granularity — not core pinning — so the node's cores stay interchangeable for
/// load balancing. See docs/storage/thread-bound-session-cursor-pool.md (§3).
///
/// Without libnuma: discovery reads sysfs and binding uses pthread_setaffinity_np
/// (see numa_topology.hpp). The parsing/lookup logic is unit-tested there.
#include "common/thread/numa_topology.hpp"

#include <folly/executors/thread_factory/NamedThreadFactory.h>
#include <folly/system/ThreadName.h>

#include <atomic>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <utility>

namespace eugraph::common {

/// True when the caller asked for NUMA binding (EUGRAPH_NUMA_BIND=1). Read once.
inline bool numaBindingEnabled() {
    static const bool enabled = [] {
        const char* v = std::getenv("EUGRAPH_NUMA_BIND");
        return v != nullptr && v[0] == '1';
    }();
    return enabled;
}

class NumaThreadFactory : public folly::NamedThreadFactory {
public:
    explicit NumaThreadFactory(std::string name)
        : folly::NamedThreadFactory(std::move(name)), topology_(NumaTopology::discover()) {}

    std::thread newThread(folly::Func&& func) override {
        const size_t node = next_node_.fetch_add(1, std::memory_order_relaxed);
        return std::thread([this, f = std::move(func), node]() mutable {
            if (topology_.has_value() && topology_->nodeCount() > 0)
                topology_->bindCurrentThreadToNode(node % topology_->nodeCount());
            f();
        });
    }

    /// How many NUMA nodes were discovered (0 = unknown/single node, binding is then skipped).
    size_t nodeCount() const {
        return topology_.has_value() ? topology_->nodeCount() : 0;
    }

private:
    std::optional<NumaTopology> topology_;
    std::atomic<size_t> next_node_{0};
};

/// Builds the thread factory for an executor: the NUMA-aware one when enabled, else folly's
/// default naming factory (identical behaviour to today).
inline std::shared_ptr<folly::ThreadFactory> makeThreadFactory(const std::string& name) {
    if (numaBindingEnabled())
        return std::make_shared<NumaThreadFactory>(name);
    return std::make_shared<folly::NamedThreadFactory>(name);
}

} // namespace eugraph::common

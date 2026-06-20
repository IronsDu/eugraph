#pragma once

#include "query/optimizer/materialization_req.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace eugraph {
namespace optimizer {

// ============================================================
// Cost — plan cost value (Columbia: COST)
//
// Simple wrapper around double. Supports infinity (upper bound
// of -1 means "no bound"). Extensible to multi-dimensional cost
// (CPU, I/O, memory) later.
// ============================================================
class Cost {
public:
    Cost() : value_(0.0) {}
    explicit Cost(double v) : value_(v) {}

    static Cost infinity() {
        return Cost(-1.0);
    }
    bool isInfinity() const {
        return value_ < 0;
    } // -1 is sentinel for infinity

    bool operator<(const Cost& o) const {
        if (isInfinity())
            return false;
        if (o.isInfinity())
            return true;
        return value_ < o.value_;
    }
    bool operator>(const Cost& o) const {
        return o < *this;
    }
    bool operator<=(const Cost& o) const {
        return !(*this > o);
    }
    bool operator>=(const Cost& o) const {
        return !(*this < o);
    }

    Cost operator+(const Cost& o) const {
        if (isInfinity() || o.isInfinity())
            return infinity();
        return Cost(value_ + o.value_);
    }
    Cost& operator+=(const Cost& o) {
        if (isInfinity() || o.isInfinity())
            *this = infinity();
        else
            value_ += o.value_;
        return *this;
    }

    double value() const {
        return value_;
    }

    std::string dump() const;

private:
    double value_;
};

// ============================================================
// PhysProp — required physical properties (Columbia: PHYS_PROP)
//
// Models two orthogonal property dimensions:
//   1. Sort order on a single property (Columbia-style).
//   2. Per-variable materialization requirements (Enricher-driven):
//      a subplan whose output PhysProp carries
//      materializations_["v"] = {need_labels=true, need_props[Person]={age,name}}
//      guarantees that variable v is materialised as a full VERTEX with
//      labels and at least the listed properties already loaded into
//      the column buffer.
//
// Sort order is satisfied by SortEnforcer; materialization requirements
// are satisfied by VertexEnrich/EdgeEnrich/PathEnrich enforcers.
// ============================================================
enum class SortOrder {
    any,
    ascending,
    descending
};

class PhysProp {
public:
    // "any" property — no requirements
    PhysProp() : order_(SortOrder::any) {}

    // Sorted property requirement
    PhysProp(uint16_t prop_id, SortOrder order) : order_(order), prop_id_(prop_id) {}

    SortOrder getOrder() const {
        return order_;
    }
    bool isAny() const {
        return order_ == SortOrder::any && materializations_.empty();
    }
    uint16_t propId() const {
        return prop_id_;
    }

    // --- Materialization accessors ---

    const VarRequirements& materializations() const {
        return materializations_;
    }
    void setMaterializations(VarRequirements reqs) {
        materializations_ = std::move(reqs);
    }
    bool hasMaterializations() const {
        for (const auto& [_, req] : materializations_) {
            if (!req.empty())
                return true;
        }
        return false;
    }

    // Does this PhysProp, as a provider, satisfy `required`?
    // Sort provider must match exactly; materialization provider must
    // be a superset (satisfiesVarRequirements semantics).
    bool satisfies(const PhysProp& required) const;

    bool operator==(const PhysProp& o) const {
        if (order_ != o.order_)
            return false;
        if (order_ != SortOrder::any && prop_id_ != o.prop_id_)
            return false;
        return materializations_ == o.materializations_;
    }
    bool operator!=(const PhysProp& o) const {
        return !(*this == o);
    }

    std::string dump() const;

private:
    SortOrder order_ = SortOrder::any;
    uint16_t prop_id_ = 0;
    VarRequirements materializations_;
};

// ============================================================
// Context — optimization context (Columbia: CONT)
//
// Pairs a required physical property with an upper cost bound.
// Used to pass optimization requirements down the operator tree.
// ============================================================
class Context {
public:
    Context(PhysProp prop, Cost upper_bound) : prop_(std::move(prop)), upper_bound_(std::move(upper_bound)) {}

    const PhysProp& getPhysProp() const {
        return prop_;
    }
    const Cost& getUpperBound() const {
        return upper_bound_;
    }
    void setUpperBound(Cost ub) {
        upper_bound_ = std::move(ub);
    }
    bool isDone() const {
        return done_;
    }
    void setDone(bool d) {
        done_ = d;
    }

    std::string dump() const;

private:
    PhysProp prop_;
    Cost upper_bound_ = Cost::infinity();
    bool done_ = false;
};

// ============================================================
// Interesting property set — which physical properties are
// "interesting" (i.e., could benefit from specific ordering).
// (Columbia: IntOrdersSet)
// ============================================================
class InterestingProps {
public:
    void add(PhysProp prop) {
        props_.push_back(std::move(prop));
    }
    const std::vector<PhysProp>& props() const {
        return props_;
    }
    bool empty() const {
        return props_.empty();
    }

private:
    std::vector<PhysProp> props_;
};

} // namespace optimizer
} // namespace eugraph

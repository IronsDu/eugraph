#pragma once

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
// Currently models sort order (property index + ascending/descending).
// "any" order means no physical property requirement.
// Extensible to distribution, partitioning, etc.
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
        return order_ == SortOrder::any;
    }
    uint16_t propId() const {
        return prop_id_;
    }

    bool operator==(const PhysProp& o) const {
        if (order_ != o.order_)
            return false;
        if (order_ == SortOrder::any)
            return true;
        return prop_id_ == o.prop_id_;
    }

    std::string dump() const;

private:
    SortOrder order_ = SortOrder::any;
    uint16_t prop_id_ = 0;
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

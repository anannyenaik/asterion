#pragma once

#include "asterion/book/l2_view.hpp"
#include "asterion/matching/matching_engine.hpp"

#include <array>
#include <cstddef>
#include <vector>

namespace asterion {

struct StrategyDecision {
  Side side{Side::None};
  OrderType order_type{OrderType::Limit};
  PriceTicks price_ticks{0};
  Quantity quantity{0};
};

struct StrategyDecisionBatch {
  std::array<StrategyDecision, 2> decisions{};
  std::size_t size{0};

  void push_back(StrategyDecision decision) noexcept {
    if (size < decisions.size()) {
      decisions[size] = decision;
      ++size;
    }
  }

  [[nodiscard]] const StrategyDecision* begin() const noexcept { return decisions.data(); }
  [[nodiscard]] const StrategyDecision* end() const noexcept { return decisions.data() + size; }
  [[nodiscard]] bool empty() const noexcept { return size == 0; }
};

class Strategy {
public:
  virtual ~Strategy() = default;
  [[nodiscard]] virtual std::vector<StrategyDecision> on_l2_update(const L2View& view) = 0;
};

} // namespace asterion

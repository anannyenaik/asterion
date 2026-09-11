#pragma once

#include "asterion/book/order.hpp"

#include <list>

namespace asterion {

struct PriceLevel {
  PriceTicks price_ticks{0};
  Quantity total_quantity{0};
  std::list<Order> orders;
};

} // namespace asterion

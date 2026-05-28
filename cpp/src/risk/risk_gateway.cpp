#include "asterion/risk/risk_gateway.hpp"

#include <algorithm>
#include <cstdlib>
#include <ios>
#include <sstream>
#include <string>
#include <utility>

namespace asterion {

namespace {

[[nodiscard]] std::int64_t abs_i64(std::int64_t value) noexcept {
  return value < 0 ? -value : value;
}

} // namespace

RiskGateway::RiskGateway(RiskLimits limits) : limits_(limits) {}

void RiskGateway::enable_kill_switch() { enable_kill_switch(0); }

void RiskGateway::enable_kill_switch(TimestampNs timestamp_ns) {
  kill_switch_.enable();
  cancel_all_working_orders(timestamp_ns, RejectReason::KillSwitch, "kill_switch_cancel");
}

void RiskGateway::on_disconnect(TimestampNs timestamp_ns) {
  if (!connected_) {
    return;
  }
  connected_ = false;
  ++disconnect_count_;
  if (limits_.cancel_on_disconnect) {
    cancel_all_working_orders(timestamp_ns, RejectReason::Disconnected, "disconnect_cancel");
  }
}

void RiskGateway::on_reconnect(TimestampNs /*timestamp_ns*/) noexcept { connected_ = true; }

void RiskGateway::on_market_data(SymbolId symbol_id, PriceTicks reference_price_ticks,
                                 TimestampNs timestamp_ns) {
  market_state_[symbol_id] = MarketState{reference_price_ticks, timestamp_ns};
}

void RiskGateway::set_position(SymbolId symbol_id, Quantity signed_position) {
  positions_[symbol_id] = signed_position;
}

Quantity RiskGateway::position(SymbolId symbol_id) const noexcept {
  const auto it = positions_.find(symbol_id);
  return it == positions_.end() ? 0 : it->second;
}

Quantity RiskGateway::working_quantity(SymbolId symbol_id) const noexcept {
  const auto it = working_quantity_.find(symbol_id);
  return it == working_quantity_.end() ? 0 : it->second;
}

std::uint64_t RiskGateway::client_symbol_key(ClientId client_id, SymbolId symbol_id) noexcept {
  return (static_cast<std::uint64_t>(client_id) << 32U) | static_cast<std::uint64_t>(symbol_id);
}

void RiskGateway::register_working_order(const NewOrderRequest& request) {
  // Only limit orders rest in the book; market orders are assumed to take
  // liquidity and never contribute to standing open-order exposure.
  if (request.order_type != OrderType::Limit) {
    return;
  }
  const WorkingOrder order{request.client_id, request.symbol_id, request.side, request.price_ticks,
                           request.quantity, kInvalidOrderId};
  working_orders_[request.client_order_id] = order;
  add_working_order_quantity(order, order.quantity);
}

void RiskGateway::add_working_order_quantity(const WorkingOrder& order, Quantity add_quantity) {
  if (add_quantity <= 0) {
    return;
  }
  working_quantity_[order.symbol_id] += add_quantity;
  if (limits_.enable_self_trade_prevention) {
    ClientWorkingBook& book =
        client_books_[client_symbol_key(order.client_id, order.symbol_id)];
    if (order.side == Side::Buy) {
      book.buy_qty_by_price[order.price_ticks] += add_quantity;
    } else if (order.side == Side::Sell) {
      book.sell_qty_by_price[order.price_ticks] += add_quantity;
    }
  }
}

void RiskGateway::release_working_order_quantity(const WorkingOrder& order,
                                                 Quantity release_quantity) {
  if (release_quantity <= 0 || order.quantity <= 0) {
    return;
  }
  const Quantity quantity = std::min(release_quantity, order.quantity);

  const auto wq_it = working_quantity_.find(order.symbol_id);
  if (wq_it != working_quantity_.end()) {
    wq_it->second -= quantity;
    if (wq_it->second <= 0) {
      working_quantity_.erase(wq_it);
    }
  }

  const auto book_it = client_books_.find(client_symbol_key(order.client_id, order.symbol_id));
  if (book_it != client_books_.end()) {
    ClientWorkingBook& book = book_it->second;
    if (order.side == Side::Buy) {
      const auto price_it = book.buy_qty_by_price.find(order.price_ticks);
      if (price_it != book.buy_qty_by_price.end()) {
        price_it->second -= quantity;
        if (price_it->second <= 0) {
          book.buy_qty_by_price.erase(price_it);
        }
      }
    } else if (order.side == Side::Sell) {
      const auto price_it = book.sell_qty_by_price.find(order.price_ticks);
      if (price_it != book.sell_qty_by_price.end()) {
        price_it->second -= quantity;
        if (price_it->second <= 0) {
          book.sell_qty_by_price.erase(price_it);
        }
      }
    }
    if (book.buy_qty_by_price.empty() && book.sell_qty_by_price.empty()) {
      client_books_.erase(book_it);
    }
  }
}

void RiskGateway::release_order(ClientOrderId client_order_id) {
  const auto it = working_orders_.find(client_order_id);
  if (it == working_orders_.end()) {
    return;
  }
  const WorkingOrder order = it->second;
  release_working_order_quantity(order, order.quantity);
  if (order.exchange_order_id != kInvalidOrderId) {
    exchange_to_client_order_ids_.erase(order.exchange_order_id);
  }
  working_orders_.erase(it);
}

void RiskGateway::on_execution_report(const ExecutionReport& report) {
  if (report.client_order_id == kInvalidClientOrderId) {
    return;
  }
  upsert_working_order_from_report(report);
}

void RiskGateway::on_execution_reports(std::span<const ExecutionReport> reports) {
  for (const ExecutionReport& report : reports) {
    on_execution_report(report);
  }
}

RiskExposureSnapshot RiskGateway::exposure_snapshot() const {
  RiskExposureSnapshot snapshot;
  snapshot.positions = positions_;
  snapshot.working_quantity = working_quantity_;
  snapshot.working_order_count = working_orders_.size();
  snapshot.kill_switch_enabled = kill_switch_.enabled();
  snapshot.connected = connected_;
  snapshot.disconnect_count = disconnect_count_;
  snapshot.disconnect_cancel_count = disconnect_cancel_count_;
  snapshot.rate_limit_mode = limits_.rate_limit_mode;
  snapshot.disconnect_order_policy = limits_.disconnect_order_policy;
  snapshot.audit_entry_count = audit_.size();
  snapshot.audit_checksum = audit_.checksum();
  return snapshot;
}

void RiskGateway::upsert_working_order_from_report(const ExecutionReport& report) {
  auto it = working_orders_.find(report.client_order_id);
  if (it == working_orders_.end() && report.exchange_order_id != kInvalidOrderId) {
    const auto mapped = exchange_to_client_order_ids_.find(report.exchange_order_id);
    if (mapped != exchange_to_client_order_ids_.end()) {
      it = working_orders_.find(mapped->second);
    }
  }
  if (it == working_orders_.end()) {
    return;
  }

  if (report.exec_type == ExecType::Rejected || report.order_status == OrderStatus::Rejected ||
      report.order_status == OrderStatus::Canceled || report.order_status == OrderStatus::Filled ||
      report.remaining_quantity <= 0) {
    release_order(report.client_order_id);
    return;
  }

  WorkingOrder& order = it->second;
  bind_exchange_order_id(order, it->first, report.exchange_order_id);
  const PriceTicks new_price =
      report.resting_price_ticks > 0 ? report.resting_price_ticks : order.price_ticks;
  const Quantity new_quantity = report.remaining_quantity;
  const bool changed = order.quantity != new_quantity || order.price_ticks != new_price;
  if (!changed) {
    return;
  }

  const WorkingOrder previous = order;
  release_working_order_quantity(previous, previous.quantity);
  order.price_ticks = new_price;
  order.quantity = new_quantity;
  add_working_order_quantity(order, order.quantity);
}

RiskGateway::WorkingOrder* RiskGateway::find_working_order_by_exchange(OrderId exchange_order_id) {
  if (exchange_order_id == kInvalidOrderId) {
    return nullptr;
  }
  const auto mapped = exchange_to_client_order_ids_.find(exchange_order_id);
  if (mapped == exchange_to_client_order_ids_.end()) {
    return nullptr;
  }
  const auto order_it = working_orders_.find(mapped->second);
  return order_it == working_orders_.end() ? nullptr : &order_it->second;
}

const RiskGateway::WorkingOrder* RiskGateway::find_working_order_by_exchange(
    OrderId exchange_order_id) const {
  if (exchange_order_id == kInvalidOrderId) {
    return nullptr;
  }
  const auto mapped = exchange_to_client_order_ids_.find(exchange_order_id);
  if (mapped == exchange_to_client_order_ids_.end()) {
    return nullptr;
  }
  const auto order_it = working_orders_.find(mapped->second);
  return order_it == working_orders_.end() ? nullptr : &order_it->second;
}

void RiskGateway::bind_exchange_order_id(WorkingOrder& order, ClientOrderId client_order_id,
                                         OrderId exchange_order_id) {
  if (exchange_order_id == kInvalidOrderId || order.exchange_order_id == exchange_order_id) {
    return;
  }
  if (order.exchange_order_id != kInvalidOrderId) {
    exchange_to_client_order_ids_.erase(order.exchange_order_id);
  }
  order.exchange_order_id = exchange_order_id;
  exchange_to_client_order_ids_[exchange_order_id] = client_order_id;
}

void RiskGateway::record_audit_entry(RiskAuditEntry entry) {
  audit_.record(std::move(entry));
  if (audit_log_.is_open()) {
    append_audit_log_entry(audit_.entries().back(), audit_.checksum());
  }
}

void RiskGateway::append_audit_log_entry(const RiskAuditEntry& entry, std::uint64_t checksum) {
  std::string line = format_risk_audit_log_entry(entry, checksum, audit_log_format_);
  const bool rotate_by_record = audit_log_max_records_per_file_ > 0 &&
                                audit_log_record_count_ >= audit_log_max_records_per_file_;
  const bool rotate_by_size = audit_log_max_bytes_per_file_ > 0 && audit_log_bytes_ > 0 &&
                              audit_log_bytes_ + line.size() > audit_log_max_bytes_per_file_;
  if (rotate_by_record || rotate_by_size) {
    rotate_audit_log();
  }
  audit_log_ << line;
  audit_log_.flush();
  ++audit_log_record_count_;
  audit_log_bytes_ += static_cast<std::uintmax_t>(line.size());
}

std::filesystem::path RiskGateway::rotated_audit_log_path(std::size_t index) const {
  if (index == 0) {
    return audit_log_base_path_;
  }
  const std::filesystem::path parent = audit_log_base_path_.parent_path();
  const std::string stem = audit_log_base_path_.stem().string();
  const std::string extension = audit_log_base_path_.extension().string();
  return parent / (stem + "." + std::to_string(index) + extension);
}

void RiskGateway::rotate_audit_log() {
  if (audit_log_.is_open()) {
    audit_log_.close();
  }
  ++audit_log_index_;
  const std::filesystem::path next_path = rotated_audit_log_path(audit_log_index_);
  audit_log_.open(next_path, std::ios::out | std::ios::app);
  audit_log_paths_.push_back(next_path);
  audit_log_record_count_ = 0;
  audit_log_bytes_ = 0;
}

void RiskGateway::cancel_all_working_orders(TimestampNs timestamp_ns, RejectReason reason,
                                            std::string_view check_name) {
  if (record_audit_) {
    for (const auto& [client_order_id, order] : working_orders_) {
      record_audit_entry(RiskAuditEntry{timestamp_ns, client_order_id, order.symbol_id, order.side,
                                        false, reason, std::string(check_name), 0,
                                        order.quantity});
      if (reason == RejectReason::Disconnected) {
        ++disconnect_cancel_count_;
      }
    }
  } else if (reason == RejectReason::Disconnected) {
    disconnect_cancel_count_ += working_orders_.size();
  }
  working_quantity_.clear();
  working_orders_.clear();
  exchange_to_client_order_ids_.clear();
  client_books_.clear();
}

bool RiskGateway::open_audit_log(const std::filesystem::path& path, RiskAuditLogFormat format) {
  return open_rotating_audit_log(path, format, 0, 0);
}

bool RiskGateway::open_rotating_audit_log(const std::filesystem::path& path,
                                          RiskAuditLogFormat format,
                                          std::size_t max_records_per_file,
                                          std::uintmax_t max_bytes_per_file) {
  close_audit_log();
  audit_log_format_ = format;
  audit_log_base_path_ = path;
  audit_log_paths_.clear();
  audit_log_index_ = 0;
  audit_log_record_count_ = 0;
  audit_log_bytes_ = 0;
  audit_log_max_records_per_file_ = max_records_per_file;
  audit_log_max_bytes_per_file_ = max_bytes_per_file;
  audit_log_.open(path, std::ios::out | std::ios::app);
  if (!audit_log_.is_open()) {
    return false;
  }
  audit_log_paths_.push_back(path);
  if (std::filesystem::exists(path)) {
    audit_log_bytes_ = std::filesystem::file_size(path);
  }
  record_audit_ = true;
  return true;
}

void RiskGateway::close_audit_log() {
  if (audit_log_.is_open()) {
    audit_log_.close();
  }
}

RiskResult RiskGateway::decide(TimestampNs now_ns, ClientOrderId client_order_id,
                               SymbolId symbol_id, Side side, std::string_view check_name,
                               bool accepted, RejectReason reason, std::int64_t limit_value,
                               std::int64_t observed_value) {
  if (record_audit_) {
    record_audit_entry(RiskAuditEntry{now_ns, client_order_id, symbol_id, side, accepted, reason,
                                      std::string(check_name),
                                      limit_value, observed_value});
  }
  return RiskResult{accepted, reason};
}

RiskResult RiskGateway::decide(const NewOrderRequest& request, TimestampNs now_ns,
                               std::string_view check_name, bool accepted, RejectReason reason,
                               std::int64_t limit_value, std::int64_t observed_value) {
  return decide(now_ns, request.client_order_id, request.symbol_id, request.side, check_name,
                accepted, reason, limit_value, observed_value);
}

RiskResult RiskGateway::check_new_order(const NewOrderRequest& request, TimestampNs now_ns) {
  if (kill_switch_.enabled()) {
    return decide(request, now_ns, "kill_switch", false, RejectReason::KillSwitch, 0, 0);
  }
  if (!connected_ &&
      limits_.disconnect_order_policy == DisconnectOrderPolicy::RejectNewOrders) {
    return decide(request, now_ns, "disconnected", false, RejectReason::Disconnected, 0, 0);
  }
  std::int64_t observed_rate = 0;
  if (!rate_limit_allows(request, now_ns, observed_rate)) {
    const std::string_view check_name =
        limits_.rate_limit_mode == RateLimitMode::SlidingWindow ? "message_rate_limit_sliding"
                                                                : "message_rate_limit";
    return decide(request, now_ns, check_name, false, RejectReason::MessageRateLimit,
                  static_cast<std::int64_t>(limits_.max_messages_per_window), observed_rate);
  }
  if (request.quantity <= 0) {
    return decide(request, now_ns, "invalid_quantity", false, RejectReason::InvalidQuantity, 0,
                  request.quantity);
  }
  if (request.order_type == OrderType::Limit && request.price_ticks <= 0) {
    return decide(request, now_ns, "invalid_price", false, RejectReason::InvalidPrice, 0,
                  request.price_ticks);
  }
  if (accepted_client_order_ids_.find(request.client_order_id) !=
      accepted_client_order_ids_.end()) {
    return decide(request, now_ns, "duplicate_client_order_id", false,
                  RejectReason::DuplicateClientOrderId, 0,
                  static_cast<std::int64_t>(request.client_order_id));
  }
  if (request.quantity > limits_.max_order_quantity) {
    return decide(request, now_ns, "max_order_quantity", false, RejectReason::MaxOrderQuantity,
                  limits_.max_order_quantity, request.quantity);
  }

  const auto market_it = market_state_.find(request.symbol_id);
  if (market_it == market_state_.end()) {
    return decide(request, now_ns, "stale_market_data", false, RejectReason::StaleMarketData,
                  limits_.stale_after_ns, 0);
  }
  const MarketState market_state = market_it->second;
  const TimestampNs age_ns = now_ns - market_state.last_timestamp_ns;
  if (limits_.stale_after_ns > 0 && age_ns > limits_.stale_after_ns) {
    return decide(request, now_ns, "stale_market_data", false, RejectReason::StaleMarketData,
                  limits_.stale_after_ns, age_ns);
  }

  if (request.order_type == OrderType::Limit &&
      abs_i64(request.price_ticks - market_state.reference_price_ticks) > limits_.price_band_ticks) {
    return decide(request, now_ns, "price_band", false, RejectReason::PriceBand,
                  limits_.price_band_ticks,
                  abs_i64(request.price_ticks - market_state.reference_price_ticks));
  }

  const std::int64_t notional = notional_for(request);
  if (notional > limits_.max_notional_ticks) {
    return decide(request, now_ns, "max_notional", false, RejectReason::MaxNotional,
                  limits_.max_notional_ticks, notional);
  }

  PriceTicks opposing_price = 0;
  if (!self_trade_check_allows(request, opposing_price)) {
    return decide(request, now_ns, "self_trade_prevention", false,
                  RejectReason::SelfTradePrevention, request.price_ticks, opposing_price);
  }

  if (limits_.max_open_order_quantity > 0) {
    const Quantity projected_working = working_quantity(request.symbol_id) + request.quantity;
    if (projected_working > limits_.max_open_order_quantity) {
      return decide(request, now_ns, "max_open_order_quantity", false,
                    RejectReason::MaxOpenOrderQuantity, limits_.max_open_order_quantity,
                    projected_working);
    }
  }

  const Quantity signed_delta = request.side == Side::Buy ? request.quantity : -request.quantity;
  const Quantity projected_position = position(request.symbol_id) + signed_delta;
  if (abs_i64(projected_position) > limits_.max_position_per_symbol) {
    return decide(request, now_ns, "max_position", false, RejectReason::MaxPosition,
                  limits_.max_position_per_symbol, abs_i64(projected_position));
  }

  const std::int64_t gross_exposure = gross_exposure_with(request);
  if (gross_exposure > limits_.max_gross_exposure_ticks) {
    return decide(request, now_ns, "max_gross_exposure", false, RejectReason::MaxGrossExposure,
                  limits_.max_gross_exposure_ticks, gross_exposure);
  }

  accepted_client_order_ids_.insert(request.client_order_id);
  if (tracks_working_orders()) {
    register_working_order(request);
  }
  return decide(request, now_ns, "accepted", true, RejectReason::None,
                limits_.max_notional_ticks, notional);
}

RiskResult RiskGateway::check_replace_order(const ReplaceOrderRequest& request,
                                            TimestampNs now_ns) {
  if (kill_switch_.enabled()) {
    return decide(now_ns, request.client_order_id, kInvalidSymbolId, Side::None, "kill_switch",
                  false, RejectReason::KillSwitch, 0, 0);
  }
  if (!connected_ &&
      limits_.disconnect_order_policy == DisconnectOrderPolicy::RejectNewOrders) {
    return decide(now_ns, request.client_order_id, kInvalidSymbolId, Side::None, "disconnected",
                  false, RejectReason::Disconnected, 0, 0);
  }
  if (request.new_quantity <= 0) {
    return decide(now_ns, request.client_order_id, kInvalidSymbolId, Side::None,
                  "replace_invalid_quantity", false, RejectReason::InvalidQuantity, 0,
                  request.new_quantity);
  }
  if (request.new_price_ticks <= 0) {
    return decide(now_ns, request.client_order_id, kInvalidSymbolId, Side::None,
                  "replace_invalid_price", false, RejectReason::InvalidPrice, 0,
                  request.new_price_ticks);
  }
  if (accepted_client_order_ids_.find(request.client_order_id) !=
      accepted_client_order_ids_.end()) {
    return decide(now_ns, request.client_order_id, kInvalidSymbolId, Side::None,
                  "replace_duplicate_client_order_id", false,
                  RejectReason::DuplicateClientOrderId, 0,
                  static_cast<std::int64_t>(request.client_order_id));
  }

  const auto mapped = exchange_to_client_order_ids_.find(request.exchange_order_id);
  if (mapped == exchange_to_client_order_ids_.end()) {
    return decide(now_ns, request.client_order_id, kInvalidSymbolId, Side::None,
                  "unknown_replace_order_id", false, RejectReason::UnknownOrder, 0,
                  static_cast<std::int64_t>(request.exchange_order_id));
  }
  auto order_it = working_orders_.find(mapped->second);
  if (order_it == working_orders_.end()) {
    return decide(now_ns, request.client_order_id, kInvalidSymbolId, Side::None,
                  "unknown_replace_order_id", false, RejectReason::UnknownOrder, 0,
                  static_cast<std::int64_t>(request.exchange_order_id));
  }

  WorkingOrder& existing = order_it->second;
  NewOrderRequest replacement{request.client_order_id,
                              existing.symbol_id,
                              existing.side,
                              OrderType::Limit,
                              request.new_price_ticks,
                              request.new_quantity,
                              request.timestamp_ns,
                              existing.client_id};

  std::int64_t observed_rate = 0;
  if (!rate_limit_allows(replacement, now_ns, observed_rate)) {
    const std::string_view check_name =
        limits_.rate_limit_mode == RateLimitMode::SlidingWindow
            ? "replace_message_rate_limit_sliding"
            : "replace_message_rate_limit";
    return decide(replacement, now_ns, check_name, false, RejectReason::MessageRateLimit,
                  static_cast<std::int64_t>(limits_.max_messages_per_window), observed_rate);
  }

  if (request.new_quantity > limits_.max_order_quantity) {
    return decide(replacement, now_ns, "replace_max_order_quantity", false,
                  RejectReason::MaxOrderQuantity, limits_.max_order_quantity,
                  request.new_quantity);
  }

  const auto market_it = market_state_.find(existing.symbol_id);
  if (market_it == market_state_.end()) {
    return decide(replacement, now_ns, "replace_stale_market_data", false,
                  RejectReason::StaleMarketData, limits_.stale_after_ns, 0);
  }
  const MarketState market_state = market_it->second;
  const TimestampNs age_ns = now_ns - market_state.last_timestamp_ns;
  if (limits_.stale_after_ns > 0 && age_ns > limits_.stale_after_ns) {
    return decide(replacement, now_ns, "replace_stale_market_data", false,
                  RejectReason::StaleMarketData, limits_.stale_after_ns, age_ns);
  }

  if (abs_i64(request.new_price_ticks - market_state.reference_price_ticks) >
      limits_.price_band_ticks) {
    return decide(replacement, now_ns, "replace_price_band", false, RejectReason::PriceBand,
                  limits_.price_band_ticks,
                  abs_i64(request.new_price_ticks - market_state.reference_price_ticks));
  }

  const std::int64_t notional = notional_for(replacement);
  if (notional > limits_.max_notional_ticks) {
    return decide(replacement, now_ns, "replace_max_notional", false,
                  RejectReason::MaxNotional, limits_.max_notional_ticks, notional);
  }

  PriceTicks opposing_price = 0;
  if (!self_trade_check_allows(replacement, opposing_price)) {
    return decide(replacement, now_ns, "replace_self_trade_prevention", false,
                  RejectReason::SelfTradePrevention, request.new_price_ticks, opposing_price);
  }

  if (limits_.max_open_order_quantity > 0) {
    const Quantity projected_working =
        working_quantity(existing.symbol_id) - existing.quantity + request.new_quantity;
    if (projected_working > limits_.max_open_order_quantity) {
      return decide(replacement, now_ns, "replace_max_open_order_quantity", false,
                    RejectReason::MaxOpenOrderQuantity, limits_.max_open_order_quantity,
                    projected_working);
    }
  }

  const Quantity signed_delta =
      existing.side == Side::Buy ? request.new_quantity : -request.new_quantity;
  const Quantity projected_position = position(existing.symbol_id) + signed_delta;
  if (abs_i64(projected_position) > limits_.max_position_per_symbol) {
    return decide(replacement, now_ns, "replace_max_position", false,
                  RejectReason::MaxPosition, limits_.max_position_per_symbol,
                  abs_i64(projected_position));
  }

  const std::int64_t gross_exposure = gross_exposure_with(replacement);
  if (gross_exposure > limits_.max_gross_exposure_ticks) {
    return decide(replacement, now_ns, "replace_max_gross_exposure", false,
                  RejectReason::MaxGrossExposure, limits_.max_gross_exposure_ticks,
                  gross_exposure);
  }

  const WorkingOrder previous = existing;
  release_working_order_quantity(previous, previous.quantity);
  existing.price_ticks = request.new_price_ticks;
  existing.quantity = request.new_quantity;
  add_working_order_quantity(existing, existing.quantity);
  accepted_client_order_ids_.insert(request.client_order_id);
  return decide(replacement, now_ns, "replace_accepted", true, RejectReason::None,
                limits_.max_notional_ticks, notional);
}

bool RiskGateway::rate_limit_allows(const NewOrderRequest& request, TimestampNs now_ns,
                                    std::int64_t& observed_value) {
  observed_value = 0;
  if (limits_.max_messages_per_window == 0 || limits_.rate_window_ns <= 0) {
    return true;
  }

  RateState& state = rate_states_[request.client_id];
  if (limits_.rate_limit_mode == RateLimitMode::SlidingWindow) {
    auto& timestamps = state.sliding_timestamps;
    while (!timestamps.empty() && now_ns - timestamps.front() >= limits_.rate_window_ns) {
      timestamps.pop_front();
    }
    timestamps.push_back(now_ns);
    observed_value = static_cast<std::int64_t>(timestamps.size());
    return timestamps.size() <= limits_.max_messages_per_window;
  }

  if (!state.active || now_ns - state.window_start_ns >= limits_.rate_window_ns) {
    state.window_start_ns = now_ns;
    state.count = 0;
    state.active = true;
  }
  ++state.count;
  observed_value = static_cast<std::int64_t>(state.count);
  return state.count <= limits_.max_messages_per_window;
}

bool RiskGateway::self_trade_check_allows(const NewOrderRequest& request,
                                          PriceTicks& opposing_price) const noexcept {
  opposing_price = 0;
  if (!limits_.enable_self_trade_prevention) {
    return true;
  }
  const auto book_it = client_books_.find(client_symbol_key(request.client_id, request.symbol_id));
  if (book_it == client_books_.end()) {
    return true;
  }

  const ClientWorkingBook& book = book_it->second;
  const bool is_market = request.order_type == OrderType::Market;
  if (request.side == Side::Buy && !book.sell_qty_by_price.empty()) {
    opposing_price = book.sell_qty_by_price.begin()->first;
    return !is_market && request.price_ticks < opposing_price;
  }
  if (request.side == Side::Sell && !book.buy_qty_by_price.empty()) {
    opposing_price = book.buy_qty_by_price.begin()->first;
    return !is_market && request.price_ticks > opposing_price;
  }
  return true;
}

std::int64_t RiskGateway::notional_for(const NewOrderRequest& request) const noexcept {
  PriceTicks price = request.price_ticks;
  if (request.order_type == OrderType::Market) {
    const auto market_it = market_state_.find(request.symbol_id);
    price = market_it == market_state_.end() ? 0 : market_it->second.reference_price_ticks;
  }
  return abs_i64(price * request.quantity);
}

std::int64_t RiskGateway::gross_exposure_with(const NewOrderRequest& request) const noexcept {
  std::int64_t gross = notional_for(request);
  for (const auto& [symbol_id, signed_position] : positions_) {
    const auto market_it = market_state_.find(symbol_id);
    if (market_it == market_state_.end()) {
      continue;
    }
    gross += abs_i64(signed_position) * market_it->second.reference_price_ticks;
  }
  return gross;
}

} // namespace asterion

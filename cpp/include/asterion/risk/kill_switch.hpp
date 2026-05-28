#pragma once

namespace asterion {

class KillSwitch {
public:
  void enable() noexcept { enabled_ = true; }
  void disable() noexcept { enabled_ = false; }
  [[nodiscard]] bool enabled() const noexcept { return enabled_; }

private:
  bool enabled_{false};
};

} // namespace asterion

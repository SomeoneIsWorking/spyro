#pragma once

#include <cstdint>

class Core;

namespace spyro {

// The observation cap counts delivered fields, but shutdown waits until the complete product
// step has returned through the framework presentation-fence check.
class RuntimeRun {
public:
  explicit RuntimeRun(std::uint64_t fieldLimit = 0) : fieldLimit_(fieldLimit) {}
  void fieldDelivered() {
    ++fields_;
  }
  void requestEnd() {
    endRequested_ = true;
  }
  bool shouldEnd() const {
    return endRequested_ || (fieldLimit_ != 0 && fields_ >= fieldLimit_);
  }
  std::uint64_t fields() const {
    return fields_;
  }

private:
  std::uint64_t fieldLimit_ = 0;
  std::uint64_t fields_ = 0;
  bool endRequested_ = false;
};

RuntimeRun &runtimeRun(Core &core);
void reportRuntimeRun(Core &core, std::uint64_t completedSteps);

} // namespace spyro

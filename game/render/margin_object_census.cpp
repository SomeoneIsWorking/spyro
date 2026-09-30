#include "margin_object_census.h"

#include "cfg.h"
#include "wide_screen_space.h"

#include <algorithm>
#include <cstdio>
#include <lucent/log.h>
#include <utility>

namespace spyro::margin_object_census {
const char *className(Class c) {
  switch (c) {
  case Class::kSector:
    return "sector";
  case Class::kMoby:
    return "moby";
  case Class::kParticle:
    return "particle";
  case Class::kGlow:
    return "glow";
  case Class::kShadow:
    return "shadow";
  case Class::kCount:
    break;
  }
  return "unknown";
}

void SpanBucket::add(int32_t x0, int32_t x1) {
  if (x1 < x0) {
    std::swap(x0, x1);
  }
  ++drawn;
  // "Outside the guest window" is about the SPAN leaving the window, not about the centre: a wide
  // object whose centre is inside still reaches the margin, and that is the case a centre test
  // would miss.
  if (x0 < 0 || x1 > wide_screen_space::kGuestClipRight) {
    ++outsideGuest;
  }
  if (x0 < minX) {
    minX = x0;
  }
  if (x1 > maxX) {
    maxX = x1;
  }
}

void SpanBucket::merge(const SpanBucket &other) {
  if (other.drawn == 0u) {
    return;
  }
  drawn += other.drawn;
  outsideGuest += other.outsideGuest;
  if (other.minX < minX) {
    minX = other.minX;
  }
  if (other.maxX > maxX) {
    maxX = other.maxX;
  }
}

bool Recorder::empty() const {
  for (const SpanBucket &t : tallies_) {
    if (!t.empty()) {
      return false;
    }
  }
  return true;
}

std::string Recorder::report() const {
  std::string out = "[margin-census] objects drawn past the guest's 512-column window\n";
  for (size_t i = 0; i < kClassCount; ++i) {
    const SpanBucket &t = tallies_[i];
    if (t.drawn == 0u) {
      char line[192];
      std::snprintf(line,
                    sizeof(line),
                    "  %-9s drawn=0       outside=0       reach=never drawn\n",
                    className(static_cast<Class>(i)));
      out += line;
      continue;
    }
    char line[224];
    std::snprintf(line,
                  sizeof(line),
                  "  %-9s drawn=%-8u outside=%-8u reach=[%d,%d]\n",
                  className(static_cast<Class>(i)),
                  t.drawn,
                  t.outsideGuest,
                  t.minX,
                  t.maxX);
    out += line;
  }
  return out;
}

void writeReportIfRequested(const Recorder &recorder) {
  const char *path = cfg_str("PSXPORT_MARGIN_CENSUS");
  if (path == nullptr || path[0] == '\0' || recorder.empty()) {
    return;
  }
  FILE *f = std::fopen(path, "wb");
  if (f == nullptr) {
    lucent::error("margin-census", "cannot open {} for the report", path);
    return;
  }
  const std::string text = recorder.report();
  std::fwrite(text.data(), 1, text.size(), f);
  std::fclose(f);
}

} // namespace spyro::margin_object_census

// The per-class drawn-reach census is the instrument that answers "did the widening reach this
// producer". An instrument that cannot be shown RED is not one, so these cases pin both directions:
// a widened run that must report objects past the guest window, and a 4:3 control that must not.
#include "margin_object_census.h"
#include "testutil.h"
#include "wide_screen_space.h"

#include <cstdint>
#include <string>

using spyro::margin_object_census::Class;
using spyro::margin_object_census::kClassCount;
using spyro::margin_object_census::Recorder;
using spyro::margin_object_census::SpanBucket;
using spyro::wide_screen_space::kGuestClipRight;

namespace {

// A class the guest can draw at 4:3: every object inside the window contributes to `drawn` and none
// to `outsideGuest`. This is the CONTROL leg, and a control that reported objects past 512 would
// mean the census is measuring something other than the widening.
void test_a_native_frame_reaches_no_object_past_the_guest_window() {
  Recorder r;
  for (int x = 0; x < 512; x += 64) {
    r.record(Class::kMoby, x, x + 32);
  }
  const auto &t = r.tally(Class::kMoby);
  CHECK_EQ(t.drawn, 8u);
  CHECK_EQ(t.outsideGuest, 0u);
  CHECK_EQ(t.minX, 0);
  CHECK_EQ(t.maxX, 544 - 32);
}

// The WIDENED leg: a 684-wide frame draws the same objects plus the ones the extra 172 columns
// revealed. `outsideGuest` is the answer the whole instrument exists to produce.
void test_a_widened_frame_reports_the_revealed_objects() {
  Recorder r;
  for (int x = 0; x < 512; x += 64) {
    r.record(Class::kMoby, x, x + 32);
  }
  r.record(Class::kMoby, 512, 560); // starts exactly on the guest's right edge
  r.record(Class::kMoby, 600, 683); // wholly inside the margin
  const auto &t = r.tally(Class::kMoby);
  CHECK_EQ(t.drawn, 10u);
  CHECK_EQ(t.outsideGuest, 2u);
  CHECK_EQ(t.maxX, 683);
  // The NEGATIVE direction: a widening must never make an in-window object count as outside, and
  // the reach must keep tracking the left side too.
  CHECK(r.tally(Class::kShadow).drawn == 0u);
}

// The boundary cases the `>` and `<` in the rule exist for. A span ending exactly ON 512 is inside;
// one starting exactly on 512 is outside; a negative x is outside at any width. Getting these
// wrong is how a census reports a widening that is off by one column.
void test_the_window_boundary_is_half_open() {
  Recorder r;
  r.record(Class::kGlow, 0, 512);   // ends on the edge: inside
  r.record(Class::kGlow, 512, 520); // starts on the edge: outside
  r.record(Class::kGlow, -4, 8);    // crosses the left edge: outside
  r.record(Class::kGlow, 100, 200); // comfortably inside
  const auto &t = r.tally(Class::kGlow);
  CHECK_EQ(t.drawn, 4u);
  CHECK_EQ(t.outsideGuest, 2u);
  CHECK_EQ(t.minX, -4);
  CHECK_EQ(t.maxX, 520);
}

// A span is about the SPAN, not the centre: a wide object whose centre is inside the window still
// reaches the margin, and a centre test would miss it. This is the case that distinguishes this
// from a per-object x test.
void test_a_wide_object_inside_the_window_still_counts_as_outside() {
  Recorder r;
  r.record(Class::kParticle, 400, 600); // centre 500, inside; right end 88px into the margin
  const auto &t = r.tally(Class::kParticle);
  CHECK_EQ(t.outsideGuest, 1u);
  CHECK_EQ(t.maxX, 600);
}

// A reversed span is normalised rather than counted as a nonsensical one. Projectors can hand back
// endpoints in either order, and an uncorrected swap would report a negative width.
void test_a_reversed_span_is_normalised() {
  Recorder r;
  r.record(Class::kSector, 600, 500);
  const auto &t = r.tally(Class::kSector);
  CHECK_EQ(t.drawn, 1u);
  CHECK_EQ(t.outsideGuest, 1u);
  CHECK_EQ(t.minX, 500);
  CHECK_EQ(t.maxX, 600);
}

// merge() is how a recipe with no Core hands its bucket to the run's recorder. It must ADD counts
// and EXTREMISE the reach; a merge that kept its own would silently lose the margin.
void test_merge_adds_counts_and_extremises_the_reach() {
  SpanBucket a;
  a.add(300, 400);
  a.add(600, 700);
  SpanBucket b;
  b.add(-20, 10);
  b.add(100, 120);
  a.merge(b);
  CHECK_EQ(a.drawn, 4u);
  CHECK_EQ(a.outsideGuest, 2u);
  CHECK_EQ(a.minX, -20);
  CHECK_EQ(a.maxX, 700);
  // Merging an empty bucket must be a no-op, or a frame that drew nothing would reset the extremes.
  SpanBucket empty;
  a.merge(empty);
  CHECK_EQ(a.drawn, 4u);
  CHECK_EQ(a.minX, -20);
  CHECK_EQ(a.maxX, 700);
}

// A fresh recorder is empty, and an empty recorder must not masquerade as a run that measured
// nothing. `empty()` is what the writer checks before producing a file at all.
void test_a_fresh_recorder_is_empty() {
  Recorder r;
  CHECK(r.empty());
  for (size_t i = 0; i < kClassCount; ++i) {
    CHECK(r.tally(static_cast<Class>(i)).drawn == 0u);
    CHECK(r.tally(static_cast<Class>(i)).minX == INT32_MAX);
  }
  r.record(Class::kMoby, 10, 20);
  CHECK(!r.empty());
}

// The report must name EVERY class, including one that never drew. A class silently absent from
// the output is indistinguishable from a class the widening never reached — which is precisely the
// question this report is asked, so the report has to distinguish them.
void test_the_report_names_every_class_even_one_that_never_drew() {
  Recorder r;
  r.record(Class::kSector, 520, 600);
  const std::string text = r.report();
  for (size_t i = 0; i < kClassCount; ++i) {
    const std::string name = spyro::margin_object_census::className(static_cast<Class>(i));
    CHECK(text.find(name) != std::string::npos);
  }
  CHECK(text.find("never drawn") != std::string::npos);
  CHECK(text.find("outside=1") != std::string::npos);
  // The denominators, not just a verdict.
  CHECK(text.find("drawn=1") != std::string::npos);
}

} // namespace

int main() {
  RUN(a_native_frame_reaches_no_object_past_the_guest_window);
  RUN(a_widened_frame_reports_the_revealed_objects);
  RUN(the_window_boundary_is_half_open);
  RUN(a_wide_object_inside_the_window_still_counts_as_outside);
  RUN(a_reversed_span_is_normalised);
  RUN(merge_adds_counts_and_extremises_the_reach);
  RUN(a_fresh_recorder_is_empty);
  RUN(the_report_names_every_class_even_one_that_never_drew);
  return 0;
}

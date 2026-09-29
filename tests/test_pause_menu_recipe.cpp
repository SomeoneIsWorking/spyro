#include "pause_menu_recipe.h"
#include "testutil.h"

// The pause menu's panel colour: DERIVED FROM THE GUEST'S OWN INSTRUCTION, not written in this
// repository. docs/issues/0144 recorded that the port asserted 0xE0 while retail stores 0x40, and
// tools/probe_pause_panel_colour.py establishes which of the handler's 29 writes to $s4 actually
// reaches the panel's three colour stores: exactly one, `addiu $s4,$zero,0x40` at 0x8001A6C8.
//
// THE WORDS BELOW ARE THE GUEST'S, LITTLE-ENDIAN. SCUS_942.28 stores instructions little-endian
// while its data is big-endian, so a word written big-endian here would decode to a different
// instruction and the test would pass on a fiction. Each is spelled with the field NAMES the
// derivation checks, which is also why the derivation checks all three.

using spyro::pause_menu::kAddImmediateOpcode;
using spyro::pause_menu::kColourRegister;
using spyro::pause_menu::kPanelColourDefinitionPc;
using spyro::pause_menu::panelColourByte;

// `addiu rt, $zero, imm` in the guest's byte order.
static std::uint32_t addiu(std::uint32_t rt, std::uint16_t immediate) {
  return (kAddImmediateOpcode << 26) | (rt << 16) | immediate;
}

// PSX 5-bit channel expansion, so the test can state the size of the defect without running a
// product.
static unsigned char expand5(std::uint32_t v) {
  return static_cast<unsigned char>((v << 3) | (v >> 2));
}

static void test_retail_colour_is_derived_from_the_guest_instruction(void) {
  // The word at kPanelColourDefinitionPc as SCUS_942.28 holds it: `addiu $s4, $zero, 0x40`.
  // 0x40 is the byte the guest stores into all three colour bytes, and the vendored decompilation
  // states it independently as `setRGB0(f4, 64, 64, 64)` for the 140..372 panel.
  const auto colour = panelColourByte(0x24140040u);
  CHECK(colour.has_value());
  CHECK_EQ(*colour, 0x40u);

  const std::uint32_t word = 0x40u | (0x40u << 8) | (0x40u << 16);
  CHECK_EQ(expand5(word & 0x1Fu), 0u);           // r
  CHECK_EQ(expand5((word >> 5) & 0x1Fu), 16u);   // g
  CHECK_EQ(expand5((word >> 10) & 0x1Fu), 132u); // b

  // The value the port used to assert, for the record: rgb(0, 57, 198) rather than rgb(0, 16, 132).
  const std::uint32_t wrong = 0xE0u | (0xE0u << 8) | (0xE0u << 16);
  CHECK_EQ(expand5((wrong >> 5) & 0x1Fu), 57u);
  CHECK_EQ(expand5((wrong >> 10) & 0x1Fu), 198u);
}

// THE GATE THAT GOES RED WHEN THE CONSTANT IS PERTURBED. If the derivation were a literal anywhere
// -- in the recipe, in the scene, or as a default on a refusal -- changing the guest's word would
// leave the answer unchanged and this case would pass on a value nothing reads. It cannot: the byte
// comes out of the word, so the word IS the input.
static void test_perturbing_the_guest_word_changes_the_colour(void) {
  const std::uint32_t bytes[] = {0x00u, 0x11u, 0x40u, 0x7Fu, 0x80u, 0xC0u, 0xE0u, 0xFFu};
  for (const std::uint32_t byte : bytes) {
    const auto colour = panelColourByte(addiu(kColourRegister, static_cast<std::uint16_t>(byte)));
    CHECK(colour.has_value());
    CHECK_EQ(*colour, byte);
  }
  // And specifically: the two values this defect is about are distinguishable OUTPUTS of the same
  // function, so neither can be a constant without the other failing.
  const auto retail = panelColourByte(0x24140040u);
  const auto former = panelColourByte(0x241400E0u);
  CHECK(retail.has_value() && former.has_value());
  CHECK(*retail != *former);
  CHECK_EQ(*retail, 0x40u);
  CHECK_EQ(*former, 0xE0u);
}

// A WORD THAT IS NOT THAT INSTRUCTION IS A REFUSAL, never a colour. Each of these is a real
// instruction the guest has elsewhere in this handler, so each is a word a wrong-address bug would
// actually hand this function rather than a synthetic shape.
static void test_a_different_instruction_refuses_rather_than_inventing_a_colour(void) {
  // `sw $s4, 0x48($sp)` at 0x8001AA14 and its siblings: a STORE, whose low half is an offset. Read
  // as an immediate it would yield 0x0048 and paint a plausible panel out of a register spill.
  CHECK(!panelColourByte(0xAFB40048u).has_value());
  // `lw $s4, 0x138($sp)` at 0x8001A42C, the handler's own register spill on entry.
  CHECK(!panelColourByte(0x8FB40138u).has_value());
  // `addiu $s4, $sp, 0x40` at 0x8001B6BC -- the right opcode and the right rt, but a COMPUTED
  // address rather than an immediate. Its low byte is 0x40, retail's colour by coincidence.
  CHECK(!panelColourByte(0x27B40040u).has_value());
  // `lui $v0, 0x8007` at 0x8001C648: right immediate shape, wrong opcode.
  CHECK(!panelColourByte(0x3C078000u).has_value());
  // `addiu $s3, $zero, 0x40` at 0x8001A9D4 -- the right opcode and the right immediate, but a
  // DIFFERENT register. This is the case that matters: a derivation checking only the opcode would
  // return 0x40 here for the wrong reason and stay right by luck.
  CHECK(!panelColourByte(addiu(19, 0x40u)).has_value());
  // An empty word -- an image that was never resident, or a short read -- must refuse, not yield
  // 0x00.
  CHECK(!panelColourByte(0x00000000u).has_value());
  // A sign-extended immediate whose low byte is the colour: the guest stores with `sb`, so only the
  // low byte ever reaches the prim, and that is what must come back.
  const auto negative = panelColourByte(addiu(kColourRegister, 0xFFC0u));
  CHECK(negative.has_value());
  CHECK_EQ(*negative, 0xC0u);
}

// Not a behavioural case but the one that keeps the two in step: the address this derivation reads
// is the one tools/probe_pause_panel_colour.py names as the only definition of $s4 reaching the
// panel's colour stores. If the probe's answer and these constants ever diverge, one of them is
// reading the wrong instruction and this is where it shows.
static void test_the_definition_site_is_the_guest_address_the_probe_named(void) {
  CHECK_EQ(kPanelColourDefinitionPc, 0x8001A6C8u);
  CHECK_EQ(kColourRegister, 20u); // $s4
  CHECK_EQ(kAddImmediateOpcode, 0x09u);
}

// The panel's OWN EXTENT, which is the number the pause-menu clip defect is measured in.
// 0x8C..0x174 is 232 authored columns, and it is that 232 the 16:9 leg is measured against, so it
// is pinned here rather than left implicit in a screenshot.
static void test_the_panels_authored_extent_is_232_columns(void) {
  spyro::pause_menu::State state;
  state.frameCounter = 1u; // past the first frame, so the guest's menu arm is the one that runs
  state.page = spyro::pause_menu::Page::Main;
  const auto recipe = spyro::pause_menu::derive(state);
  CHECK(recipe.gui);
  CHECK_EQ(recipe.panelX0, 0x8C);
  CHECK_EQ(recipe.panelX1, 0x174);
  CHECK_EQ(recipe.panelX1 - recipe.panelX0, 232);

  // The options page is the guest's OTHER box (0x8001A8F4), and it is WIDER. A clip measured
  // against the main page's extent would under-clip or over-clip by this much, so the number is
  // recorded.
  state.page = spyro::pause_menu::Page::Options;
  const auto options = spyro::pause_menu::derive(state);
  CHECK_EQ(options.panelX0, 0x54);
  CHECK_EQ(options.panelX1, 0x1AC);
  CHECK_EQ(options.panelX1 - options.panelX0, 344);
}

int main(void) {
  RUN(retail_colour_is_derived_from_the_guest_instruction);
  RUN(perturbing_the_guest_word_changes_the_colour);
  RUN(a_different_instruction_refuses_rather_than_inventing_a_colour);
  RUN(the_definition_site_is_the_guest_address_the_probe_named);
  RUN(the_panels_authored_extent_is_232_columns);
  return pt_summary();
}

#include "menu_panel_submit.h"

#include "core.h"
#include "game.h"
#include "render_queue.h"

namespace spyro::menu_panel {

void submitPanel(Core *core,
                 RenderQueue &queue,
                 const pause_menu::PanelRect &authored,
                 Colour colour,
                 std::int32_t shift) {
  const GpuState gpu = core->game->gpu;
  const pause_menu::PanelRect panel{authored.x0 + gpu.s_off_x + shift,
                                    authored.y0 + gpu.s_off_y,
                                    authored.x1 + gpu.s_off_x + shift,
                                    authored.y1 + gpu.s_off_y};
  const int xs[4] = {panel.x0, panel.x1, panel.x0, panel.x1};
  const int ys[4] = {panel.y0, panel.y0, panel.y1, panel.y1};
  const int us[4] = {};
  const int vs[4] = {};
  const unsigned char rs[4] = {colour.r, colour.r, colour.r, colour.r};
  const unsigned char gs[4] = {colour.g, colour.g, colour.g, colour.g};
  const unsigned char bs[4] = {colour.b, colour.b, colour.b, colour.b};
  queue.emitOrQueue(core,
                    1,
                    RQ_HUD,
                    RQ_OM_2D_FG,
                    4,
                    pause_menu::kPanelStp,
                    0,
                    xs,
                    ys,
                    nullptr,
                    nullptr,
                    us,
                    vs,
                    rs,
                    gs,
                    bs,
                    nullptr,
                    3,
                    0,
                    0,
                    0,
                    0,
                    gpu.s_tw_mx,
                    gpu.s_tw_my,
                    gpu.s_tw_ox,
                    gpu.s_tw_oy,
                    gpu.s_da_x0,
                    gpu.s_da_y0,
                    panel.x1,
                    gpu.s_da_y1,
                    gpu.s_tp_blend);
}

void submitBorder(Core *core,
                  RenderQueue &queue,
                  std::span<const pause_menu::Segment> authoredSegments,
                  std::int32_t drawAreaX1,
                  std::int32_t shift) {
  const GpuState gpu = core->game->gpu;
  for (const pause_menu::Segment &authored : authoredSegments) {
    pause_menu::Segment segment = pause_menu::placeSegment(authored, {gpu.s_off_x, gpu.s_off_y});
    segment.x0 += shift;
    segment.x1 += shift;
    // No endpoint-ordering test: the guest's box walks right-to-left on its bottom edge and
    // bottom-to-top on its left one. Only the clip is a real constraint.
    if (segment.x1 >= drawAreaX1 || segment.x0 < gpu.s_da_x0 || segment.y1 < gpu.s_da_y0) {
      continue;
    }
    const pause_menu::Rgb start = pause_menu::borderColour(segment.shade0);
    const pause_menu::Rgb end = pause_menu::borderColour(segment.shade1);
    const int xs[4] = {segment.x0, segment.x1, segment.x0, segment.x1};
    const int ys[4] = {segment.y0, segment.y1, segment.y0, segment.y1};
    const int us[4] = {};
    const int vs[4] = {};
    const unsigned char rs[4] = {start.r, end.r, start.r, end.r};
    const unsigned char gs[4] = {start.g, end.g, start.g, end.g};
    const unsigned char bs[4] = {start.b, end.b, start.b, end.b};
    queue.emitOrQueue(core,
                      1,
                      RQ_HUD,
                      RQ_OM_2D_FG,
                      2,
                      0,
                      0,
                      xs,
                      ys,
                      nullptr,
                      nullptr,
                      us,
                      vs,
                      rs,
                      gs,
                      bs,
                      nullptr,
                      0,
                      0,
                      0,
                      0,
                      0,
                      gpu.s_tw_mx,
                      gpu.s_tw_my,
                      gpu.s_tw_ox,
                      gpu.s_tw_oy,
                      gpu.s_da_x0,
                      gpu.s_da_y0,
                      drawAreaX1 - 1,
                      gpu.s_da_y1,
                      0);
  }
}

} // namespace spyro::menu_panel

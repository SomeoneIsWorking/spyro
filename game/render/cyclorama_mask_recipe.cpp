#include "cyclorama_mask_recipe.h"

#include "core.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace spyro::cyclorama_mask_recipe {
namespace {

constexpr size_t kFaceCapacity = 64;

struct WorkVertex {
  Vertex vertex{};
  double x = 0.0;
  double y = 0.0;
};

Recipe refuse(Recipe out, Status status, const char *why) {
  out.status = status;
  out.refusal = why;
  out.faces.clear();
  return out;
}

double side(const cyclorama_portal_mesh::ClipEdge &edge, double x, double y) {
  return (double)(edge.x1 - edge.x0) * (y - edge.y0) - (double)(edge.y1 - edge.y0) * (x - edge.x0);
}

WorkVertex
intersection(const WorkVertex &from, const WorkVertex &to, double fromSide, double toSide) {
  const double denominator = fromSide - toSide;
  const double t = denominator == 0.0 ? 0.0 : fromSide / denominator;
  WorkVertex out{};
  out.x = from.x + (to.x - from.x) * t;
  out.y = from.y + (to.y - from.y) * t;
  out.vertex.sx = (int16_t)std::clamp<long long>(std::llround(out.x), INT16_MIN, INT16_MAX);
  out.vertex.sy = (int16_t)std::clamp<long long>(std::llround(out.y), INT16_MIN, INT16_MAX);
  out.vertex.screenX = (float)out.x;
  out.vertex.screenY = (float)out.y;
  return out;
}

std::vector<WorkVertex> clip(std::array<WorkVertex, 3> triangle,
                             const std::vector<cyclorama_portal_mesh::ClipEdge> &edges) {
  std::vector<WorkVertex> polygon(triangle.begin(), triangle.end());
  for (const auto &edge : edges) {
    if (polygon.empty()) {
      break;
    }
    std::vector<WorkVertex> next;
    next.reserve(polygon.size() + 1u);
    WorkVertex previous = polygon.back();
    double previousSide = side(edge, previous.x, previous.y);
    bool previousInside = previousSide > 0.0;
    for (const WorkVertex &current : polygon) {
      const double currentSide = side(edge, current.x, current.y);
      const bool currentInside = currentSide > 0.0;
      if (currentInside != previousInside) {
        next.push_back(intersection(previous, current, previousSide, currentSide));
      }
      if (currentInside) {
        next.push_back(current);
      }
      previous = current;
      previousSide = currentSide;
      previousInside = currentInside;
    }
    polygon = std::move(next);
  }
  return polygon;
}

} // namespace

Recipe build(Core *core, const cyclorama_portal_mesh::PortalFrame &frame) {
  Recipe out{};
  out.portalOrdinal = frame.portalOrdinal;
  out.otBin = frame.otBin;
  if (core == nullptr) {
    return out;
  }
  if (!frame.maskVisible || frame.edges.empty()) {
    out.status = Status::ValidEmpty;
    out.refusal = "none";
    return out;
  }
  if (frame.status != cyclorama_portal_mesh::Status::ValidEmpty &&
      frame.status != cyclorama_portal_mesh::Status::Ready &&
      frame.status != cyclorama_portal_mesh::Status::NearFamilyUnsupported) {
    return refuse(std::move(out), Status::InvalidFrame, "portal_frame");
  }
  if (frame.edges.size() > 32u) {
    return refuse(std::move(out), Status::InvalidClipRegion, "edge_capacity");
  }

  const std::array<std::array<std::array<int32_t, 2>, 3>, 2> source = {{
      {{{0, 0}, {512, 0}, {512, 240}}},
      {{{0, 0}, {512, 240}, {0, 240}}},
  }};
  const uint32_t color = frame.status == cyclorama_portal_mesh::Status::NearFamilyUnsupported
                             ? core->mem_r32(frame.asset + 0x10u)
                             : frame.tintColor;
  for (const auto &sourceTriangle : source) {
    std::array<WorkVertex, 3> triangle{};
    for (size_t i = 0; i < triangle.size(); ++i) {
      triangle[i].x = sourceTriangle[i][0];
      triangle[i].y = sourceTriangle[i][1];
      triangle[i].vertex.sx = (int16_t)sourceTriangle[i][0];
      triangle[i].vertex.sy = (int16_t)sourceTriangle[i][1];
      triangle[i].vertex.screenX = (float)sourceTriangle[i][0];
      triangle[i].vertex.screenY = (float)sourceTriangle[i][1];
    }
    const auto polygon = clip(triangle, frame.edges);
    if (polygon.size() < 3u) {
      continue;
    }
    for (size_t i = 1; i + 1u < polygon.size(); ++i) {
      if (out.faces.size() == kFaceCapacity) {
        return refuse(std::move(out), Status::CapacityExceeded, "face_capacity");
      }
      Face face{};
      face.rgb = color & 0x00ffffffu;
      face.vertices = {polygon[0].vertex, polygon[i].vertex, polygon[i + 1u].vertex};
      out.faces.push_back(face);
    }
  }
  if (out.faces.empty()) {
    out.status = Status::ValidEmpty;
    out.refusal = "none";
    return out;
  }
  out.status = Status::Ready;
  out.refusal = "none";
  return out;
}

const char *statusName(Status status) {
  switch (status) {
  case Status::Ready:
    return "ready";
  case Status::ValidEmpty:
    return "valid empty";
  case Status::InvalidCore:
    return "invalid core";
  case Status::InvalidFrame:
    return "invalid frame";
  case Status::InvalidClipRegion:
    return "invalid clip region";
  case Status::CapacityExceeded:
    return "capacity exceeded";
  }
  return "unknown";
}

} // namespace spyro::cyclorama_mask_recipe

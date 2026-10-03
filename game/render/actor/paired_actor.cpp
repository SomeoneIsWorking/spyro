#include "paired_actor.h"

#include "paired_actor_pose.h"
#include "paired_actor_producer.h"

#include <array>
#include <lucent/log.h>

bool spyro::paired_actor::buildTransform(Core *c, spyro::paired_actor::Transform &out) {
  return build_transform(c, out);
}

bool spyro::paired_actor::decodePose(Core *c) {
  std::array<LayerDesc, kLayers> desc;
  PairedPose pose;
  std::array<uint32_t, kLayers> decoded{};
  const bool descriptors = build_descs(c, desc);
  const bool ok = descriptors && decode_pose(c, desc, pose, decoded);
  lucent::debug("pairedpose",
                "0x80023AC4 pose: scanned_layers=3 valid_descriptors={} "
                "layer0={}/{} layer1={}/{} layer2={}/{} emitted_faces=0",
                descriptors ? 3 : 0,
                decoded[0],
                descriptors ? desc[0].a.count : 0,
                decoded[1],
                descriptors ? desc[1].a.count : 0,
                decoded[2],
                descriptors ? desc[2].a.count : 0);
  return ok;
}

bool spyro::paired_actor::submit(Core *c, spyro::paired_actor::FrameState &state) {
  return spyro::paired_actor_producer::captureAndSubmit(c, state, false);
}

bool spyro::paired_actor::submitField(Core *c, spyro::paired_actor::FrameState &state) {
  return spyro::paired_actor_producer::captureAndSubmit(c, state, true);
}

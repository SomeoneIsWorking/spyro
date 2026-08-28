#include "spyro1_frame_policy.h"

using spyro1::FieldCadence;

int main() {
  FieldCadence cadence;
  cadence.beginLogicFrame();
  if (cadence.fields() != 0 || cadence.completesLogicFrame()) {
    return 1;
  }
  cadence.delivered();
  if (cadence.fields() != 1 || cadence.completesLogicFrame()) {
    return 2;
  }
  cadence.delivered();
  if (cadence.fields() != FieldCadence::kMinimumFieldsPerProductStep ||
      !cadence.completesLogicFrame()) {
    return 3;
  }
  cadence.beginLogicFrame();
  return cadence.completesLogicFrame() ? 4 : 0;
}

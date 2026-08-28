#pragma once

class Core;

namespace spyro2 {

void completeDrawSync(Core &core);
void armGpuTimeout(Core &core);
void checkGpuTimeout(Core &core);

} // namespace spyro2

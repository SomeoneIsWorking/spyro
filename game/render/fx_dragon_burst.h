#pragma once

class Core;

// Direct native owner of the dragon-rescue burst 0x80058864. 0x8001CFDC calls it before every one
// of its state branches while D_80076248 is armed, and an inactive gate is a valid empty frame
// rather than a refusal. Returns false only when the producer cannot represent the live state.
bool dragon_burst_submit(Core *core);

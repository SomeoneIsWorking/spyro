#pragma once

class Core;

// 0x80019698, the model chain in the order that routine calls it: regular actors, the composed
// secondary/shaded pass, moby shadows, Spyro's model and shadow, his flame, and the glow/sparkle
// pair. FIELD and the dragon cutscene's state 0 both call it whole, so it has one owner rather than
// a second transcription of the same seven layers. Returns the address of the layer that refused,
// or 0 when the whole chain composed.
unsigned spyro_field_model_chain_submit(Core *core);

# NWN:EE shadow-map injector

Language for the injector’s rendering and weather effects.

## Language

**Wet film**:
The rain-driven wet coating on surfaces, including its animated fine detail and sheen; surface impact ripples have been removed. Its ON state means the full detailed effect; it is separate from standing puddles.
_Avoid_: Simple wet film (retired mode)

**Rain puddles**:
The standing-water appearance on rainy surfaces, including its reflections and refraction.

**Private rain**:
The injector’s falling rain particles and particle splashes, independently controlled from wet film and rain puddles.
_Avoid_: Shader rain when referring to all rain-related effects

**Rain-shelter map** (precipitation occlusion):
The cached overhead depth map of NWN's static opaque and alpha/card blockers. It determines where roofs and other scenery shelter surfaces and precipitation. Its resolution is separate from puddle reflections and sun shadows, even though the baseline allocation currently follows the sun-map size.

**Sun receiver tags**:
Per-sample depth, depth span and engine-derived creature/highlight flags written beside native geometry. The final shadow pass uses them to classify the visible surface. Linux keeps their reserved texture attached for one native scene pass, restoring draw buffers after every draw and removing the attachment before postprocessing.

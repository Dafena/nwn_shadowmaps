# NWN:EE shadow-map injector

Language for the injector’s rendering and weather effects.

## Language

**Wet film**:
The rain-driven wet coating on surfaces, including animated fine detail and sheen. It is separate from standing puddles.
_Avoid_: Simple wet film (retired mode)

**Rain puddles**:
The standing-water appearance on rainy surfaces, including its reflections and refraction.

**Private rain**:
The injector’s falling rain particles and particle splashes, independently controlled from wet film and rain puddles.
_Avoid_: Shader rain when referring to all rain-related effects

**Rain-shelter map** (precipitation occlusion):
The overhead representation of roofs and other static scenery that determines which surfaces and precipitation are sheltered. It is separate from puddle reflections and sun shadows.

**Sun receiver tags**:
The classification and depth information identifying the visible surface as scenery, a creature or a highlighted object when applying sun shadows. Classification follows the game’s actual rendered surfaces and coverage.

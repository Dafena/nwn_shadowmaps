# Build libnwn_shadowmap.so
# subhook/ (bundled) is compiled as C; the hook is compiled as C++.
#
# nwn_alphasort.cpp is retained for reference (its ELF .symtab resolver is the
# basis for the one in nwn_shadowmap.cpp) but is no longer built or injected:
# per-triangle sorting did not fix the transparency it was written for, and it
# detours RenderFlat, which the shadow pass replays through.
#
# imgui/ (vendored, MIT, see imgui/LICENSE.txt) backs the settings overlay. It
# is the project's only external dependency and is deliberately confined to
# nwn_overlay_imgui.cpp -- no ImGui header reaches nwn_shadowmap.cpp. Only the
# OpenGL3 backend is vendored: there is no platform backend, because input is
# polled from statically-linked SDL rather than taken from NWN's event queue.
# ImGui's own sources are compiled without -Wall -Wextra; they are upstream
# code and their warnings would bury ours.
SUBHOOK ?= ./subhook
CC  ?= gcc
CXX ?= g++
CFLAGS   ?= -O2 -fPIC -I$(SUBHOOK)
CXXFLAGS ?= -O2 -std=c++17 -fPIC -Wall -Wextra -I$(SUBHOOK)
IMGUI_DIR  = imgui
# -fvisibility=hidden matters here: this is an LD_PRELOAD library, so anything
# it exports interposes on the whole process. ImGui's ~900 symbols have no
# business being visible to NWN; hidden still links fine inside the .so.
IMGUI_FLAGS = -O2 -std=c++17 -fPIC -fvisibility=hidden -fvisibility-inlines-hidden \
              -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends

TARGET = libnwn_shadowmap.so

IMGUI_OBJS = imgui.o imgui_draw.o imgui_tables.o imgui_widgets.o imgui_impl_opengl3.o
OBJS = nwn_shadowmap.o shadow_config.o shadow_math.o nwn_oit.o nwn_overlay_imgui.o subhook.o $(IMGUI_OBJS)

all: $(TARGET)

# ---------------------------------------------------------------------------
#  make deploy -- the build you hand to a tester
# ---------------------------------------------------------------------------
# Same source, -DNWN_SHADOWMAP_SHIPPING, into a SEPARATE object dir so the two
# builds can coexist without a stale-object trap. See NWN_SHIP in
# nwn_platform.h for what the flag changes. In short: it carries its own
# defaults (no launcher script needed -- plain LD_PRELOAD works), hides every
# control that REMOVES shadows, writes no .pgm dumps, and runs no frame-cost
# instrumentation (which costs a glFinish per reporting frame).
DEPLOY_TARGET = libnwn_shadowmap_deploy.so
DEPLOY_DIR    = build-deploy
DEPLOY_OBJS   = $(addprefix $(DEPLOY_DIR)/,$(OBJS))

deploy: $(DEPLOY_TARGET)

$(DEPLOY_TARGET): $(DEPLOY_OBJS)
	$(CXX) -shared $^ -o $@ -ldl
	@echo "built $@ -- copy it next to nwmain-linux and LD_PRELOAD it"

$(DEPLOY_DIR):
	mkdir -p $(DEPLOY_DIR)

$(DEPLOY_DIR)/%.o: %.cpp | $(DEPLOY_DIR)
	$(CXX) $(CXXFLAGS) -DNWN_SHADOWMAP_SHIPPING -c $< -o $@

# nwn_shadowmap.cpp is split into same-translation-unit .inc modules.  Keep the
# shipping object on the same dependency graph as the development object;
# otherwise `make deploy` can silently reuse a stale object after an .inc-only
# fix (as happened with the startup settings reconciliation).
$(DEPLOY_DIR)/nwn_shadowmap.o: nwn_shadowmap.cpp \
	shadow_gl_api.inc shadow_engine_bindings.inc weather_runtime.inc \
	shadow_targets.inc shadow_diagnostics_settings.inc shadow_replay.inc \
	shadow_shader_interposition.inc shadow_fullscreen_receiver.inc \
	shadow_overlay_runtime.inc shadow_trace_cascade.inc shadow_local_lights.inc \
	nwn_overlay.h nwn_hooks_core.h nwn_platform.h shadow_config.h shadow_math.h \
	| $(DEPLOY_DIR)
	$(CXX) $(CXXFLAGS) -DNWN_SHADOWMAP_SHIPPING -c $< -o $@

# The overlay needs the ImGui include paths and the hidden-visibility flags,
# exactly as the normal build's rule does -- the generic rule above does not
# carry them, which is why this one is spelled out.
$(DEPLOY_DIR)/nwn_overlay_imgui.o: nwn_overlay_imgui.cpp nwn_overlay.h | $(DEPLOY_DIR)
	$(CXX) $(CXXFLAGS) -DNWN_SHADOWMAP_SHIPPING \
	    -fvisibility=hidden -fvisibility-inlines-hidden \
	    -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends -c $< -o $@

$(DEPLOY_DIR)/subhook.o: $(SUBHOOK)/subhook.c | $(DEPLOY_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Explicit, not a %-rule: a pattern stem cannot be empty, so imgui%.o never
# matched imgui.o and make reported "no rule to make target".
$(DEPLOY_DIR)/imgui.o: $(IMGUI_DIR)/imgui.cpp | $(DEPLOY_DIR)
	$(CXX) $(IMGUI_FLAGS) -c $< -o $@
$(DEPLOY_DIR)/imgui_draw.o: $(IMGUI_DIR)/imgui_draw.cpp | $(DEPLOY_DIR)
	$(CXX) $(IMGUI_FLAGS) -c $< -o $@
$(DEPLOY_DIR)/imgui_tables.o: $(IMGUI_DIR)/imgui_tables.cpp | $(DEPLOY_DIR)
	$(CXX) $(IMGUI_FLAGS) -c $< -o $@
$(DEPLOY_DIR)/imgui_widgets.o: $(IMGUI_DIR)/imgui_widgets.cpp | $(DEPLOY_DIR)
	$(CXX) $(IMGUI_FLAGS) -c $< -o $@
$(DEPLOY_DIR)/imgui_impl_opengl3.o: $(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp | $(DEPLOY_DIR)
	$(CXX) $(IMGUI_FLAGS) -c $< -o $@

.PHONY: deploy

$(TARGET): $(OBJS)
	$(CXX) -shared $^ -o $@ -ldl

nwn_shadowmap.o: nwn_shadowmap.cpp \
	shadow_gl_api.inc shadow_engine_bindings.inc weather_runtime.inc \
	shadow_targets.inc shadow_diagnostics_settings.inc shadow_replay.inc \
	shadow_shader_interposition.inc shadow_fullscreen_receiver.inc \
	shadow_overlay_runtime.inc shadow_trace_cascade.inc shadow_local_lights.inc \
	nwn_overlay.h nwn_hooks_core.h nwn_platform.h shadow_config.h shadow_math.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

shadow_config.o: shadow_config.cpp shadow_config.h nwn_platform.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

shadow_math.o: shadow_math.cpp shadow_math.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Order-independent transparency, ported from the console renderer. Its own
# module (own env vars, own launcher, own toggle) but deliberately the SAME .so:
# two LD_PRELOADs would chain their interposers fine and still be wrong, because
# each replays geometry through the other's per-draw hook and a re-entrancy flag
# cannot be shared across two dlopen'd libraries. See nwn_hooks_core.h.
nwn_oit.o: nwn_oit.cpp nwn_hooks_core.h nwn_platform.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

nwn_overlay_imgui.o: nwn_overlay_imgui.cpp nwn_overlay.h
	$(CXX) $(CXXFLAGS) -fvisibility=hidden -fvisibility-inlines-hidden \
	    -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends -c $< -o $@

imgui.o: $(IMGUI_DIR)/imgui.cpp
	$(CXX) $(IMGUI_FLAGS) -c $< -o $@
imgui_draw.o: $(IMGUI_DIR)/imgui_draw.cpp
	$(CXX) $(IMGUI_FLAGS) -c $< -o $@
imgui_tables.o: $(IMGUI_DIR)/imgui_tables.cpp
	$(CXX) $(IMGUI_FLAGS) -c $< -o $@
imgui_widgets.o: $(IMGUI_DIR)/imgui_widgets.cpp
	$(CXX) $(IMGUI_FLAGS) -c $< -o $@
imgui_impl_opengl3.o: $(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp
	$(CXX) $(IMGUI_FLAGS) -c $< -o $@

subhook.o: $(SUBHOOK)/subhook.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS)
.PHONY: all clean

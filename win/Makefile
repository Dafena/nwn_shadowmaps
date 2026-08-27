# Cross-build the full Windows injector from Linux with mingw-w64.
#
#   cd win && make
#
# Output: version.dll (PE32+, matching nwmain.exe). Drop it next to
# nwmain.exe in bin/win32 and launch the game normally -- no launcher script,
# no env wrapper. Accepted Mode 2 material routing defaults on for Windows;
# environment variables can still override shipping defaults. The injector
# writes nwn_shadowmap_win.log beside the exe when logging is requested because
# a GUI process has no usable stderr.
#
# Same sources as the .so: the engine logic is shared, with nwn_platform.h
# shimming dlsym/mprotect/sysconf and four #ifdef'd sites (symbol resolution,
# GL interposition, the fault guard, SDL_PollEvent).
CXX   = x86_64-w64-mingw32-g++
CC    = x86_64-w64-mingw32-gcc
ROOT  = ..
IMGUI = $(ROOT)/imgui

# -DSUBHOOK_STATIC keeps subhook's symbols internal; -D_WIN32_WINNT for the
# Win32 APIs used by the platform shim.
COMMON = -O2 -fPIC -D_WIN32_WINNT=0x0601 -DSUBHOOK_STATIC \
         -I$(ROOT) -I$(ROOT)/subhook -I$(IMGUI) -I$(IMGUI)/backends
CXXFLAGS = $(COMMON) -std=c++17 -Wall -Wextra
IMGUIFLAGS = $(COMMON) -std=c++17 -fvisibility=hidden -fvisibility-inlines-hidden

# -static-libgcc/-static-libstdc++ matter: the target machine will not have
# mingw's runtime DLLs, and a proxy that fails to load takes the game with it.
LDFLAGS = -shared -static -static-libgcc -static-libstdc++ \
          -lopengl32 -lgdi32 -luser32 -lversion

TARGET = version.dll
OBJS = nwn_win_proxy.o nwn_shadowmap.o shadow_config.o shadow_math.o \
       nwn_platform_win.o nwn_overlay_imgui.o \
       nwn_oit.o subhook.o imgui.o imgui_draw.o imgui_tables.o imgui_widgets.o imgui_impl_opengl3.o

all: $(TARGET)

verify: $(TARGET)
	./check_artifact.sh "$(TARGET)" "$(NWN_WIN_EXE)"

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) version.def $(LDFLAGS) -o $@
	@echo "--- exports (must be ALL 17 version.dll entry points) ---"
	@x86_64-w64-mingw32-objdump -p $@ | grep -E "^\s+\[\s*[0-9]+\]" | head -20

nwn_win_proxy.o: nwn_win_proxy.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@
# The renderer now lives in .inc modules included by this one TU, so they are
# dependencies too. Without them a Linux-side change compiles into the .so and
# silently does NOT reach the DLL -- the exact trap that left version.dll a day
# stale before.
INC_MODULES = $(wildcard $(ROOT)/shadow_*.inc)

nwn_shadowmap.o: $(ROOT)/nwn_shadowmap.cpp $(ROOT)/nwn_platform.h $(ROOT)/nwn_hooks_core.h \
                 $(ROOT)/shadow_config.h $(ROOT)/shadow_math.h $(INC_MODULES) nwn_win_symbols.h
	$(CXX) $(CXXFLAGS) -c $< -o $@
# Shares nwn_oit.cpp with the .so exactly as nwn_shadowmap.cpp is shared: the
# module resolves its own GL through the nwn_platform.h dlsym shim, so it needs
# no #ifdef of its own.
nwn_oit.o: $(ROOT)/nwn_oit.cpp $(ROOT)/nwn_hooks_core.h $(ROOT)/nwn_platform.h
	$(CXX) $(CXXFLAGS) -c $< -o $@
# Separate objects since the refactor: pure helpers with no renderer state.
shadow_config.o: $(ROOT)/shadow_config.cpp $(ROOT)/shadow_config.h $(ROOT)/nwn_platform.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

shadow_math.o: $(ROOT)/shadow_math.cpp $(ROOT)/shadow_math.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

# THIS OBJECT CARRIES THE BUILD STAMP (__DATE__/__TIME__ in the log banner), so
# it must be rebuilt whenever ANYTHING is rebuilt -- otherwise the stamp is the
# last time this one file changed, not the build, and a tester's log reports a
# stale build date. That happened on 2026-08-15 and sent us reading a log
# against the wrong source.
nwn_platform_win.o: $(ROOT)/nwn_platform_win.cpp $(ROOT)/nwn_platform.h \
                    $(ROOT)/nwn_shadowmap.cpp $(INC_MODULES) \
                    $(ROOT)/nwn_overlay_imgui.cpp $(ROOT)/shadow_config.cpp \
                    $(ROOT)/shadow_math.cpp nwn_win_symbols.h
	$(CXX) $(CXXFLAGS) -c $< -o $@
nwn_overlay_imgui.o: $(ROOT)/nwn_overlay_imgui.cpp $(ROOT)/nwn_overlay.h
	$(CXX) $(IMGUIFLAGS) -Wall -c $< -o $@
subhook.o: $(ROOT)/subhook/subhook.c
	$(CC) $(COMMON) -c $< -o $@
imgui.o: $(IMGUI)/imgui.cpp
	$(CXX) $(IMGUIFLAGS) -c $< -o $@
imgui_draw.o: $(IMGUI)/imgui_draw.cpp
	$(CXX) $(IMGUIFLAGS) -c $< -o $@
imgui_tables.o: $(IMGUI)/imgui_tables.cpp
	$(CXX) $(IMGUIFLAGS) -c $< -o $@
imgui_widgets.o: $(IMGUI)/imgui_widgets.cpp
	$(CXX) $(IMGUIFLAGS) -c $< -o $@
imgui_impl_opengl3.o: $(IMGUI)/backends/imgui_impl_opengl3.cpp
	$(CXX) $(IMGUIFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS)
.PHONY: all verify clean

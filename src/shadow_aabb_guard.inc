// Linux NWN 89.8193.37-17 allocates PartAABB's scratch stream using indexed
// vertices, but writes three expanded vertices for each face in a color run.
// A valid dense WOK can therefore corrupt memory with renderaabb enabled.
// Reserve the native maximum before the first scratch allocation. Never lie
// about the capacity of an existing allocation; unknown binaries fail closed.
static void ensure_aabb_debug_capacity() {
#ifndef _WIN32
    static bool checked = false;
    if (checked) return;
    checked = true;
    auto* draw = static_cast<unsigned char*>(dlsym(RTLD_DEFAULT, "_ZN8PartAABB4DrawEv"));
    if (!draw) return;
    const unsigned char countLoad[] = {0x41,0x0f,0xb7,0x96,0x84,0x02,0,0,0x8b,0x0d};
    const unsigned char streamLoad[] = {0x48,0x8b,0x3d};
    const unsigned char savedPointer[] = {0x48,0x89,0x45,0xb8};
    for (size_t i = 11; i + 14 <= 512; ++i) {
        if (memcmp(draw+i, countLoad, sizeof(countLoad)) ||
            memcmp(draw+i-11, streamLoad, sizeof(streamLoad)) ||
            memcmp(draw+i-4, savedPointer, sizeof(savedPointer))) continue;
        int32_t capOffset = 0, streamOffset = 0;
        memcpy(&capOffset, draw+i+10, sizeof(capOffset));
        memcpy(&streamOffset, draw+i-8, sizeof(streamOffset));
        auto* capacity = reinterpret_cast<unsigned*>(draw+i+14+capOffset);
        auto** stream = reinterpret_cast<void**>(draw+i-4+streamOffset);
        const unsigned old = *capacity;
        if (*stream || old < 16 || old > 65536 || (old & (old-1))) {
            fprintf(stderr, "[shadowmap] AABB scratch guard skipped: already allocated or unexpected capacity %u\n", old);
            return;
        }
        *capacity = 65536; // 21,845 native triangles * 3, rounded up.
        fprintf(stderr, "[shadowmap] AABB scratch guard: %u -> %u expanded vertices\n", old, *capacity);
        return;
    }
    fprintf(stderr, "[shadowmap] AABB scratch guard: unsupported engine signature; unchanged\n");
#endif
}

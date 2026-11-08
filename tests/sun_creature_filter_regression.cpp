#include "../nwn_shadowmap.cpp"

static GLuint boundFbo = 42;
static void bind_fbo(GLenum, GLuint fbo) { boundFbo = fbo; }
int main(int argc, char **argv) {
    gl::BindFramebuffer = bind_fbo;
    g_sunObjectCaptureActive = true;
    g_sunObjectFbo = 43;
    g_cascadeDynamicFbo = 42;
    g_cascadeReplayLayer = 0;
    int failures = 0;
    for (auto kind :
         {DrawOwnerKind::Creature, DrawOwnerKind::NonCreature, DrawOwnerKind::Unknown}) {
        g_currentDrawOwnerKind = kind;
        int common = 0, filtered = 0;
        boundFbo = 42;
        ++common; // Same call order as every native draw wrapper.
        DUPLICATE_SELECTIVE_LOCAL_DEPTH(++filtered, (void)0);
        const bool ok =
            common == 1 && filtered == (kind == DrawOwnerKind::Creature ? 0 : 1) && boundFbo == 42;
        printf("sun caster type=%d common=%d non-creature=%d restored=%d %s\n", (int)kind, common,
               filtered, boundFbo == 42, ok ? "PASS" : "FAIL");
        if (!ok)
            ++failures;
    }
    std::string shader =
        "#version 330 core\nout vec4 compat_glFragColor;\n"
        "uniform float alpha;\nvoid main(){compat_glFragColor=vec4(0.7,0.6,0.5,alpha);"
        "if(alpha<0.01)discard; if(alpha<0.2)return;}\n";
    const bool patched = inject_sun_receiver_tag(shader);
    const bool wrapped = shader.find("void main(){float nwnSunDepthSpan=") != std::string::npos &&
                         shader.find("nwnSunTaggedMain();") != std::string::npos;
    const bool duplicate = inject_sun_receiver_tag(shader);
    printf("sun native receiver shader: patched=%d wrapped-return=%d idempotent=%d\n", patched,
           wrapped, !duplicate);
    if (!patched || !wrapped || duplicate)
        ++failures;
    if (argc == 2) {
        FILE *out = fopen(argv[1], "wb");
        if (!out)
            return 1;
        fwrite(shader.data(), 1, shader.size(), out);
        fclose(out);
    }
    return failures ? 1 : 0;
}

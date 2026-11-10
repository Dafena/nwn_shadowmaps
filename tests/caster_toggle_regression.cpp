// Exercise the actual replay function with a recording GL backend.
#include "../nwn_shadowmap.cpp"
#include <array>
static int mutations = 0, drawFbo = 41, readFbo = 43, clears = 0;
static GLint vpNow[4] = {17, 23, 1920, 1006};
static GLboolean depthNow = GL_FALSE;
static bool depthEnabled = false, scissorEnabled = true;
static void get_int(GLenum key, GLint *out) {
  if (key == GL_VIEWPORT) {
    std::memcpy(out, vpNow, sizeof(vpNow));
  } else
    *out = (key == GL_DRAW_FRAMEBUFFER_BINDING   ? drawFbo
            : key == GL_READ_FRAMEBUFFER_BINDING ? readFbo
                                                 : 0);
}
static void get_bool(GLenum key, GLboolean *out) {
  if (key == GL_COLOR_WRITEMASK)
    for (int i = 0; i < 4; ++i)
      out[i] = GL_TRUE;
  else
    *out = depthNow;
}
static GLboolean is_enabled(GLenum k) {
  return k == GL_SCISSOR_TEST ? scissorEnabled
         : k == GL_DEPTH_TEST ? depthEnabled
                              : false;
}
static void bind_fbo(GLenum k, GLuint v) {
  ++mutations;
  if (k != GL_READ_FRAMEBUFFER)
    drawFbo = v;
  if (k != GL_DRAW_FRAMEBUFFER)
    readFbo = v;
}
static void viewport(GLint x, GLint y, GLsizei w, GLsizei h) {
  ++mutations;
  vpNow[0] = x;
  vpNow[1] = y;
  vpNow[2] = w;
  vpNow[3] = h;
}
static void enable(GLenum k) {
  ++mutations;
  if (k == GL_DEPTH_TEST)
    depthEnabled = true;
  if (k == GL_SCISSOR_TEST)
    scissorEnabled = true;
}
static void disable(GLenum k) {
  ++mutations;
  if (k == GL_DEPTH_TEST)
    depthEnabled = false;
  if (k == GL_SCISSOR_TEST)
    scissorEnabled = false;
}
static void depth_mask(GLboolean v) {
  ++mutations;
  depthNow = v;
}
static void layer(GLenum, GLenum, GLuint, GLint, GLint) { ++mutations; }
static void clear_depth(GLclampd) {}
static void clear(GLbitfield) { ++clears; }
static void color_mask(GLboolean, GLboolean, GLboolean, GLboolean) {}
static void active_texture(GLenum) {}
static void bind_texture(GLenum, GLuint) {}
int main() {
  gl::GetIntegerv = get_int;
  gl::GetBooleanv = get_bool;
  gl::IsEnabled = is_enabled;
  gl::BindFramebuffer = bind_fbo;
  gl::Viewport = viewport;
  gl::Enable = enable;
  gl::ClearDepth = clear_depth;
  gl::Clear = clear;
  gl::ColorMask = color_mask;
  gl::ActiveTexture = active_texture;
  gl::BindTexture = bind_texture;
  gl::Disable = disable;
  gl::DepthMask = depth_mask;
  gl::FramebufferTextureLayer = layer;
  alignas(float) static unsigned char stack[MTX_ENTRIES * MTX_STRIDE] = {};
  static int mode = 0;
  eng::MtxStack = stack;
  eng::CurMtxIdx = &mode;
  g_receiverEnabled = true;
  g_cascadeTargetsUsable = true;
  g_shadowFrameContext.valid = true;
  g_shadowFrameContext.serial = 42;
  g_cascadeMath.valid = true;
  g_cascadeMath.sourceFrame = 42;
  g_staticWorldEnabled = false;
  g_cascadeStaticFbo = 1;
  g_cascadeStaticTex = 2;
  g_cascadeDynamicFbo = 3;
  g_cascadeDynamicTex = 4;
  g_cascadeActiveCount = 4;
  g_cascadeDynamicLayers = 4;
  int failures = 0;
  for (bool worldMap : {false, true})
    for (bool isStatic : {false, true}) {
      g_staticWorldEnabled = worldMap;
      g_staticNearCascades = 1;
      ++g_shadowFrameContext.serial;
      g_cascadeMath.sourceFrame = g_shadowFrameContext.serial;
      unsigned char originalStack[sizeof(stack)];
      for (size_t i = 0; i < sizeof(stack); ++i)
        stack[i] = (unsigned char)i;
      std::memcpy(originalStack, stack, sizeof(stack));
      g_staticCastersEnabled = !isStatic;
      g_dynamicCastersEnabled = isStatic;
      mutations = clears = 0;
      drawFbo = 41;
      readFbo = 43;
      vpNow[0] = 17;
      vpNow[1] = 23;
      vpNow[2] = 1920;
      vpNow[3] = 1006;
      depthNow = GL_FALSE;
      depthEnabled = false;
      scissorEnabled = true;
      replay_bucket_into_cascade_layers(nullptr, isStatic ? 0 : 2, isStatic);
      bool restored = drawFbo == 41 && readFbo == 43 && vpNow[0] == 17 &&
                      vpNow[1] == 23 && vpNow[2] == 1920 && vpNow[3] == 1006 &&
                      !depthNow && !depthEnabled && scissorEnabled;
      bool fresh = true;
      for (int i = 0; i < 4; ++i)
        fresh &= (isStatic ? g_cascadeStaticCaptureFrameLayer[i]
                           : g_cascadeDynamicCaptureFrameLayer[i]) ==
                 g_shadowFrameContext.serial;
      bool matrices =
          std::memcmp(originalStack, stack, sizeof(stack)) == 0 && mode == 0;
      printf("%s disabled (world=%d): GL restored=%d matrices=%d empty-map "
             "fresh=%d clears=%d %s\n",
             isStatic ? "fixed" : "moving", worldMap, restored, matrices, fresh,
             clears,
             (restored && fresh && matrices && clears == 4) ? "PASS" : "FAIL");
      if (!restored || !fresh || !matrices || clears != 4)
        ++failures;
    }
  // A zero-layer configuration must return before changing any GL state.
  g_cascadeDynamicLayers = 0;
  mutations = 0;
  replay_bucket_into_cascade_layers(nullptr, 2, false);
  printf("zero dynamic layers: GL mutations=%d %s\n", mutations,
         mutations ? "FAIL" : "PASS");
  if (mutations)
    ++failures;
  return failures ? 1 : 0;
}

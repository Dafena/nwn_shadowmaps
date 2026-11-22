// Real injected wet-surface shader regression; fixture generated from
// production strings.
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/glcorearb.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
static GLuint program(const char *path) {
  std::ifstream f(path);
  std::string fs{std::istreambuf_iterator<char>(f), {}};
  const char *vs = "#version 330 core\nvoid main(){vec2 "
                   "p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4("
                   "p*2.-1.,0.,1.);}";
  GLuint p = glCreateProgram();
  for (int i = 0; i < 2; ++i) {
    GLuint s = glCreateShader(i ? GL_FRAGMENT_SHADER : GL_VERTEX_SHADER);
    const char *c = i ? fs.c_str() : vs;
    glShaderSource(s, 1, &c, 0);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
      char log[8192];
      glGetShaderInfoLog(s, sizeof(log), 0, log);
      puts(log);
      exit(1);
    }
    glAttachShader(p, s);
    glDeleteShader(s);
  }
  glLinkProgram(p);
  GLint ok;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[8192];
    glGetProgramInfoLog(p, sizeof(log), 0, log);
    puts(log);
    exit(1);
  }
  return p;
}
int main(int argc, char **argv) {
  auto get = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress(
      "eglGetPlatformDisplayEXT");
  EGLDisplay d = get(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, 0);
  EGLint ma, mi;
  if (!eglInitialize(d, &ma, &mi)) {
    puts("EGL unavailable");
    return 1;
  }
  eglBindAPI(EGL_OPENGL_API);
  EGLConfig c;
  EGLint n;
  EGLint at[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,
                 EGL_OPENGL_BIT, EGL_NONE};
  eglChooseConfig(d, at, &c, 1, &n);
  EGLint ca[] = {EGL_CONTEXT_MAJOR_VERSION,
                 3,
                 EGL_CONTEXT_MINOR_VERSION,
                 3,
                 EGL_CONTEXT_OPENGL_PROFILE_MASK,
                 EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                 EGL_NONE};
  auto ctx = eglCreateContext(d, c, EGL_NO_CONTEXT, ca);
  EGLint sa[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
  auto surf = eglCreatePbufferSurface(d, c, sa);
  if (!eglMakeCurrent(d, surf, surf, ctx))
    return 2;
  printf("GPU: %s\n", glGetString(GL_RENDERER));
  GLuint vao;
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  const int W = 256, H = 256;
  GLuint tex, fbo;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, W, H, 0, GL_RGBA, GL_FLOAT, 0);
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         tex, 0);
  glViewport(0, 0, W, H);
  GLuint inputs[4];
  glGenTextures(4, inputs);
  for (int i = 0; i < 4; ++i) {
    glActiveTexture(GL_TEXTURE0 + i);
    glBindTexture(GL_TEXTURE_2D, inputs[i]);
    std::vector<float> data(256 * 256 * 4);
    for (size_t j = 0; j < data.size(); ++j)
      data[j] = i == 2 ? 1.f : float((j * 73) % 251) / 251.f;
    if (i == 2) {
      glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, 256, 256, 0,
                   GL_DEPTH_COMPONENT, GL_FLOAT, data.data());
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE,
                      GL_COMPARE_REF_TO_TEXTURE);
    } else
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 256, 256, 0, GL_RGBA, GL_FLOAT,
                   data.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  }
  std::vector<float> rippleReference,candidateReference;
  for (int a = 1; a < argc; ++a) {
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, inputs[2]);
    std::vector<float> exposedDepth(256 * 256, 1.0f);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 256, GL_DEPTH_COMPONENT,
                    GL_FLOAT, exposedDepth.data());
    auto p = program(argv[a]);
    glUseProgram(p);
    auto uf = [&](const char *n, float v) {
      glUniform1f(glGetUniformLocation(p, n), v);
    };
    auto ui = [&](const char *n, int v) {
      glUniform1i(glGetUniformLocation(p, n), v);
    };
    auto mat = [&](const char *n, const float *v) {
      glUniformMatrix4fv(glGetUniformLocation(p, n), 1, GL_FALSE, v);
    };
    float id[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float view[16];
    std::copy(id, id + 16, view);
    view[14] = -20;
    mat("m_view", view);
    mat("nwnWeatherFbView", view);
    view[14] = 20;
    mat("m_view_inv", view);
    mat("nwnWeatherFbProj", id);
    mat("nwnWeatherFbProjInv", id);
    float occ[16] = {.01f, 0, 0, 0, 0, .01f, 0, 0, 0, 0, .001f, 0, 0, 0, 0, 1};
    mat("nwnWeatherPrecipitationOcclusionVP", occ);
    glUniform2f(glGetUniformLocation(p, "pixelParams"), 1.f / W, 1.f / H);
    uf("farClip", 100);
    uf("nwnWeatherEligible", 1);
    uf("nwnWeatherRainAmount", 1);
    uf("nwnWeatherTime", 100);
    uf("nwnWeatherReflectionReady", 1);
    uf("nwnWeatherNoiseReady", 1);
    uf("nwnWeatherPrecipitationOcclusionReady", 1);
    const char* fixture=std::strrchr(argv[a],'/');fixture=fixture?fixture+1:argv[a];
    const bool isProfile=std::strncmp(fixture,"profile-",8)==0;
    int profileMode=isProfile?std::atoi(fixture+8):0;
    if(std::strstr(fixture,"candidate-fast"))profileMode=6;
    ui("nwnWetFilmProbe",profileMode);
    ui("nwnWetFilmDisabled", std::strstr(argv[a], "film-off") ? 1 : 0);
    ui("nwnRainDisablePuddles", std::strstr(argv[a], "puddles-off") ? 1 : 0);
    ui("nwnWeatherNoiseTex", 0);
    ui("nwnWeatherFbColor", 1);
    ui("nwnWeatherPrecipitationOcclusionTex", 2);
    ui("nwnWeatherFbDepth", 3);
    // All sampler types need distinct valid units even when a branch is
    // inactive.
    ui("nwnWeatherSnowDeformationTex", 0);
    ui("nwnWeatherSnowLightData", 0);
    // Exercise the actual complete rain branch, including derivative use next
    // to varying puddle/ground decisions. No retyped rain implementation.
    std::vector<float> dry(W * H * 4), wet(W * H * 4), sheltered(W * H * 4);
    auto draw = [&](std::vector<float> &pixels) {
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, pixels.data());
      if (glGetError() != GL_NO_ERROR) {
        puts("FAIL: GL state/error");
        exit(1);
      }
      for (float v : pixels)
        if (!std::isfinite(v)) {
          puts("FAIL: non-finite surface output");
          exit(1);
        }
    };
    if(std::strstr(fixture,"candidate-")) {
      std::vector<float> edgeDepth(256*256);
      for(int y=0;y<256;++y)for(int x=0;x<256;++x)edgeDepth[y*256+x]=(x<128 || (x%19<2 && y%13<7))?0.4990f:1.0f;
      glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,inputs[2]);
      glTexSubImage2D(GL_TEXTURE_2D,0,0,0,256,256,GL_DEPTH_COMPONENT,GL_FLOAT,edgeDepth.data());
    }
    uf("nwnWeatherRainAmount", 0);
    draw(dry);
    uf("nwnWeatherRainAmount", 1);
    draw(wet);
    if(std::strstr(fixture,"candidate-")) {
      if(std::strstr(fixture,"candidate-base"))candidateReference=wet;
      else {
        if(candidateReference.size()!=wet.size())return 17;
        float error=0;for(size_t i=0;i<wet.size();++i)error=std::max(error,std::abs(wet[i]-candidateReference[i]));
        if(error>0.003922f){printf("FAIL: equivalent shelter changed material by %g\n",error);return 18;}
        printf("PASS: production-injected shelter candidate material error=%g\n",error);
      }
      glDeleteProgram(p);continue;
    }
    if (isProfile) {
      const float expected[6][3]={{1,0,1},{1,0,0},{0,0,1},{1,0,1},{0,0,1},{0,0,0}};
      if(profileMode<0 || profileMode>5)return 14;
      for(size_t i=0;i<wet.size();++i)if(i%4<3 && wet[i]!=expected[profileMode][i%4]){
        printf("FAIL: native probe mode %d did not isolate actual field entries\n",profileMode);return 15;}
      printf("PASS: native probe mode %d field/exposure isolation\n",profileMode);
      glDeleteProgram(p);continue;
    }
    if (std::strstr(argv[a], "gradient")) {
      const bool degenerate = std::strstr(argv[a], "degenerate") != nullptr;
      const float expected[] = {degenerate ? 0.0f : 0.25f,
                                degenerate ? 0.0f : -0.5f, 0.0f, 1.0f};
      for (size_t i = 0; i < wet.size(); ++i)
        if (std::abs(wet[i] - expected[i % 4]) > 0.001f) {
          puts("FAIL: noise gradient changed with pixel footprint or became "
               "singular");
          return 1;
        }
      printf("PASS: %s known world gradient and singular-footprint guard\n",
             argv[a]);
      glDeleteProgram(p);
      continue;
    }
    if (std::strstr(argv[a], "trace")) {
      const int requested = std::strstr(argv[a], "trace3")    ? 3
                            : std::strstr(argv[a], "trace5")  ? 5
                            : std::strstr(argv[a], "trace10") ? 10
                                                              : 0;
      ui("nwnPuddleTraceSteps", requested);
      draw(wet);
      const float expected[] = {float(requested ? requested : 10), 0, 0, 1};
      for (size_t i = 0; i < wet.size(); ++i)
        if (std::abs(wet[i] - expected[i % 4]) > 0.001f) {
          puts(
              "FAIL: reflection trace did not obey the selected sample budget");
          return 1;
        }
      printf("PASS: %s actual reflection trace loop budget\n", argv[a]);
      glDeleteProgram(p);
      continue;
    }
    if (std::strstr(argv[a], "optics")) {
      const bool disabled = std::strstr(argv[a], "puddles-off") != nullptr;
      float maximum = 0;
      for (size_t i = 0; i < wet.size(); i += 4)
        maximum = std::max(maximum, wet[i]);
      if ((disabled && maximum != 0) || (!disabled && maximum < 2)) {
        puts("FAIL: puddle switch did not gate actual reflection and "
             "refraction work");
        return 1;
      }
      printf("PASS: %s actual optical work %s\n", argv[a],
             disabled ? "skipped" : "executed");
      glDeleteProgram(p);
      continue;
    }
    if (std::strstr(argv[a], "ripple-")) {
      if (std::strstr(argv[a], "reference"))
        rippleReference = wet;
      else {
        if (rippleReference.size() != wet.size())
          return 6;
        float error = 0;
        for (size_t i = 0; i < wet.size(); ++i)
          error = std::max(error, std::abs(wet[i] - rippleReference[i]));
        if (std::strstr(argv[a], "ripple-film-compact")) {
          float heightError = 0, gradientError = 0;
          for (size_t i = 0; i < wet.size(); ++i) {
            float e = std::abs(wet[i] - rippleReference[i]);
            if (i % 4 == 0) heightError = std::max(heightError, e);
            if (i % 4 == 1 || i % 4 == 2)
              gradientError = std::max(gradientError, e);
          }
          // Nine omitted tails: each height <= exp(-58*0.375^2),
          // each gradient length <= 116*0.375/0.5*height*1.45.
          // The final gradient clamp is a non-expansive ball projection.
          if (heightError > 0.003f || gradientError > 0.33f) {
            printf("FAIL: compact film support height=%g gradient=%g\n",
                   heightError, gradientError);
            return 7;
          }
          printf("PASS: %s compact support height error=%g gradient error=%g\n",
                 argv[a], heightError, gradientError);
          glDeleteProgram(p);
          continue;
        }
        if (error > 0.0001f) {
          printf("FAIL: bounded ripple changed field by %g\n", error);
          return 7;
        }
        printf("PASS: %s bounded ripple maximum error=%g\n", argv[a], error);
      }
      glDeleteProgram(p);
      continue;
    }
    if (std::strstr(argv[a], "film-fields")) {
      float maximum = 0;
      for (size_t i = 0; i < wet.size(); i += 4)
        maximum = std::max(maximum, wet[i]);
      bool detailed =
          !std::strstr(argv[a], "film-off");
      if ((detailed && maximum != 1) || (!detailed && maximum != 0)) {
        puts("FAIL: film mode did not gate real noise/ripple evaluations");
        return 8;
      }
      printf("PASS: %s real film field work %s\n", argv[a],
             detailed ? "executed" : "skipped");
      glDeleteProgram(p);
      continue;
    }
    if (std::strstr(argv[a], "film-off-puddles-off")) {
      for (size_t i = 0; i < wet.size(); ++i)
        if (std::abs(wet[i] - dry[i]) > 1e-6f) {
          puts("FAIL: disabled film/puddles still modify rainy surfaces");
          return 9;
        }
      puts("PASS: film and puddles off preserve native surface colour");
      glDeleteProgram(p);
      continue;
    }
    float maximumChange = 0;
    for (size_t i = 0; i < dry.size(); ++i)
      maximumChange = std::max(maximumChange, std::abs(dry[i] - wet[i]));
    if (maximumChange < 0.01f) {
      puts("FAIL: wet surface did not render");
      exit(1);
    }
    for (int kind = 0; kind < 2; ++kind) {
      uf("nwnWeatherNoiseReady", float(kind));
      uf("nwnWeatherEligible", 2);
      draw(wet); // Alpha/card: wet film, no puddles.
      uf("nwnWeatherEligible", 0);
      draw(sheltered);
     // Non-static: native output.
      for (size_t i = 0; i < dry.size(); ++i)
        if (std::abs(dry[i] - sheltered[i]) > 1e-5f) {
          puts("FAIL: weather leaked onto ineligible material");
          exit(1);
        }
    }
    uf("nwnWeatherEligible", 1);
    // A roof blocks both puddles and the film at the actual shader call site.
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, inputs[2]);
    std::vector<float> roof(256 * 256, 0);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 256, GL_DEPTH_COMPONENT,
                    GL_FLOAT, roof.data());
    draw(sheltered);
    for (size_t i = 0; i < dry.size(); ++i)
      if (std::abs(dry[i] - sheltered[i]) > 0.001f) {
        printf("FAIL: rain leaked through roof delta=%g index=%zu\n",
               std::abs(dry[i] - sheltered[i]), i);
        exit(1);
      }
    printf("PASS: %s wet/dry, opaque/alpha, texture/procedural noise, finite "
           "derivatives and shelter gates\n",
           argv[a]);
    glDeleteProgram(p);
  }
}

#include "../nwn_shadowmap.cpp"
#include <fstream>
static int uploaded = -1, uploads = 0, disabled = -1, film = -1;
static void uniform_int(GLint location, GLint value) {
  if (location == 9)
    film = value;
  if (location == 8)
    disabled = value;
  if (location == 7) {
    uploaded = value;
    ++uploads;
  }
}
static void uniform_float(GLint, GLfloat) {}
int main(int argc, char **argv) {
  if(argc==3 && std::strcmp(argv[1],"--dump-weather")==0) {
    std::string source=R"GLSL(#version 330 core
#define SHADER_TYPE 2
#define POSITION_VIEW 1
#define VERTEX_NORMAL 1
#define FOG 0
#define varying in
#define highp
#define texture2D texture
varying highp vec3 vPosView;
uniform highp mat4 m_view_inv;
uniform mat4 m_view;
uniform vec2 pixelParams;
uniform float farClip;
uniform int areaFlags;
vec3 vFragmentNormal;
vec3 vSurfaceNormal;
vec4 FragmentColor;
out vec4 result;
#define gl_FragColor result
void main(){
vec3 vPosView=vec3((gl_FragCoord.xy*pixelParams-0.5)*80.0,-20.0);
vSurfaceNormal=vec3(0,0,1);vFragmentNormal=vSurfaceNormal;
FragmentColor=vec4(0.4,0.45,0.5,1);
gl_FragColor = FragmentColor;
}
)GLSL";
    g_wetFilmProfiling=true;g_wetFilmCandidateProfile=true;
    if(!weather_runtime::patch_fragment_shader(source) ||
       source.find("nwnWeatherPrecipitationExposureFast(")==std::string::npos)return 20;
    std::ofstream output(argv[2]);output<<source;return output?0:21;
  }
  gl::Uniform1i = uniform_int;
  gl::Uniform1f = uniform_float;
  weather_runtime::ProgramState material;
  material.weatherEligible = 1;
  material.puddleTraceSteps = 7;
  material.disablePuddles = 8;
  material.wetFilmDisabled = 9;
  for (bool enabled : {true, false, true}) {
    g_wetFilmEnabled = enabled;
    weather_runtime::push_draw_state(&material, 0.0f);
    if (film != (enabled ? 0 : 1))
      return 6;
  }
  for (bool enabled : {true, false, true}) {
    g_rainPuddlesEnabled = enabled;
    weather_runtime::push_draw_state(&material, 0.0f);
    if (disabled != (enabled ? 0 : 1))
      return 4;
  }
  const int expected[] = {10, 5, 3};
  for (int quality = 0; quality < 3; ++quality) {
    g_puddleReflectionQuality = quality;
    weather_runtime::push_draw_state(&material, 0.0f);
    if (uploaded != expected[quality])
      return 1;
    int count = uploads;
    weather_runtime::push_draw_state(&material, 0.0f);
    if (uploads != count)
      return 2;
  }
  g_wetFilmEnabled=true;g_wetFilmProfiling=true;g_wetFilmProbeMode=5;
  weather_runtime::push_draw_state(&material,0.0f);
  if(film!=1 || !g_wetFilmEnabled)return 15;
  g_wetFilmProfiling=false;
  weather_runtime::push_draw_state(&material,0.0f);
  if(film!=0)return 16;
  g_wetFilmProbeMode=0;
  SettingRef settings[kSettingsMax];
  const unsigned n = settings_table(settings, kSettingsMax);
  bool found = false;
  for (unsigned i = 0; i < n; ++i)
    if (!std::strcmp(settings[i].key, "puddle_reflection_quality"))
      found = settings[i].i == &g_puddleReflectionQuality;
  if (!found)
    return 3;
  found = false;
  for (unsigned i = 0; i < n; ++i)
    if (!std::strcmp(settings[i].key, "rain_puddles"))
      found = settings[i].b == &g_rainPuddlesEnabled;
  if (!found)
    return 5;
  found = false;
  for (unsigned i = 0; i < n; ++i)
    if (!std::strcmp(settings[i].key, "wet_film_enabled"))
      found = settings[i].b == &g_wetFilmEnabled;
  if (!found)
    return 7;
  found = false;
  for (unsigned i = 0; i < n; ++i)
    if (!std::strcmp(settings[i].key, "weather_shelter_size"))
      found = settings[i].i == &g_weatherShelterSize;
  if (!found) return 12;
  char temporary[] = "/tmp/nwn-wet-film-settings.XXXXXX";
  char *directory = mkdtemp(temporary);
  if (!directory)
    return 8;
  setenv("NWN_SHADOWMAP_OUT_DIR", directory, 1);
  setenv("NWN_SHADOWMAP_NO_SETTINGS", argc > 1 ? "1" : "0", 1);
  const std::string path =
      std::string(directory) + "/nwn_shadowmap_settings.ini";
  auto load = [&](const char *body, bool expected) {
    {
      std::ofstream file(path);
      file << "version=" << kSettingsVersion << "\n" << body;
    }
    g_wetFilmEnabled = !expected;
    load_settings();
    return g_wetFilmEnabled == expected;
  };
  if (argc > 1) {
    {
      std::ofstream file(path);
      file << "version=" << kSettingsVersion << "\nwet_film_enabled=0\n";
    }
    g_wetFilmEnabled = true;
    load_settings();
    if (!g_wetFilmEnabled)
      return 11;
  } else if (!load("wet_film_quality=0\n", true) ||
             !load("wet_film_quality=1\n", false) ||
             !load("wet_film_quality=2\n", false) ||
             !load("wet_film_enabled=1\nwet_film_quality=1\n", true) ||
             !load("wet_film_quality=0\nwet_film_enabled=0\n", false))
    return 9;
  if (argc == 1) {
    std::ofstream file(path);
    file << "version=" << kSettingsVersion << "\nweather_shelter_size=2048\n";
    file.close(); g_weatherShelterSize=0;load_settings();
    if(g_weatherShelterSize!=2048)return 13;
  }
  g_weatherShelterSize=1024;settings_reset_defaults();
  if(g_weatherShelterSize!=0)return 14;
  std::remove(path.c_str());
  rmdir(directory);
  puts("PASS: ON/OFF film, legacy migration, settings-disable semantics and "
       "trace budgets");
}

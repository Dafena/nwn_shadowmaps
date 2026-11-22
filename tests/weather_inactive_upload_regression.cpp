#define main checkpoint_tag_state_main
#include "sun_receiver_tag_state_regression.cpp"
#undef main
#include <map>
static std::map<GLint,float> values;
static unsigned timeWrites=0,scalarWrites=0;
static void scalar(GLint location,GLfloat value){values[location]=value;++scalarWrites;if(location==4)++timeWrites;}
static void integer(GLint location,GLint value){values[location]=float(value);}
int main(){
 using namespace weather_runtime;
 gl::Uniform1f=scalar;gl::Uniform1i=integer;
 ProgramState state;state.program=77;state.weatherEligible=1;state.rainAmount=2;state.snowAmount=3;state.weatherTime=4;
 state.wetFilmDisabled=5;state.disablePuddles=6;
 inactiveUploadProbe=true;g.rainAmount=0;g.snowAmount=0;
 push_draw_state(&state,0);
 if(!state.inactiveUploadPrimed || timeWrites!=1)return 1;
 scalarWrites=0;push_draw_state(&state,2);
 if(timeWrites!=1 || scalarWrites!=3 || values[1]!=2)return 2;
 g.rainAmount=.002f;push_draw_state(&state,1);
 if(timeWrites!=2 || values[2]!=.002f)return 3;
 g.rainAmount=0;g.snowAmount=.002f;push_draw_state(&state,1);
 if(timeWrites!=3 || values[3]!=.002f)return 4;
 g.snowAmount=.001f;g.rainAmount=.001f;push_draw_state(&state,0);
 if(timeWrites!=3 || values[1]!=0 || values[2]!=.001f || values[3]!=.001f)return 5;
 inactiveUploadProbe=false;push_draw_state(&state,0);if(timeWrites!=4)return 6;
 g.programCount=1;g.programs[0]=state;g_realLinkProgram=nullptr;my_link_program(77);
 if(g.programs[0].inactiveUploadPrimed)return 7;
 puts("Inactive weather: initializes payload, preserves per-draw classification, resumes rain/snow, threshold boundary and relink invalidation: PASS");
}

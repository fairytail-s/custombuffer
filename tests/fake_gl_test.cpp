// Host-side check of the hook logic against a fake GL (no device needed):
//   g++ -std=c++20 -O1 -Iinclude -Ivendor tests/fake_gl_test.cpp -o /tmp/fake_gl_test -ldl && /tmp/fake_gl_test
#include "../src/GlHooks.cpp"
#include <cstdio>
#include <map>
#include <vector>
#include <string>
namespace pl::memory { int hook(FuncPtr, FuncPtr, FuncPtr*, HookPriority) { return -1; } bool unhook(FuncPtr, FuncPtr) { return false; } }

using namespace buffers;
// ---------------- fake GL ----------------
namespace fake {
using G = buffers::GLenum;
std::map<std::string,int> calls;
int curProgram=0, active=0, drawFbo=0, readFbo=0, rb=0, readBuffer=0x8CE0, nextTex=100, nextFbo=10;
int bound[32];
unsigned lastDepthFormat=0; int blendOn=1, colorMaskOn=1, colorClears=0; std::vector<int> castFbos; int shadowPassVal=0; int shadowDraws=0, shadowDrawFbo=-1, shadowDrawVp=0, shadowTexAtDraw=-1; int depthFunc=0x0203, depthMask=0, scissorOn=1, depthTest=1, polyOff=0; int vp[4]={0,0,2400,1200};

int sceneTexSamples=0; bool fboComplete=true; int progUnit[16][2]; // [prog][kind]
int drawSeenUnit3=-1; int errorOnce=0;
#define C(n) ++calls[n]
void GetIntegerv(G p, int* v){ C("GetIntegerv");
  switch(p){case 0x8B8D:*v=curProgram;break;case 0x84E0:*v=0x84C0+active;break;case 0x8069:*v=bound[active];break;
  case 0x8CA6:*v=drawFbo;break;case 0x8CAA:*v=readFbo;break;case 0x8CA7:*v=rb;break;case 0x0BA2:for(int i=0;i<4;i++)v[i]=vp[i];break;
  case 0x0C02:*v=readBuffer;break;case 0x0B74:*v=depthFunc;break;case 0x0B72:*v=depthMask;break;case 0x80A9:*v=0;break;default:*v=0;} }
unsigned char IsEnabled(G c){C("IsEnabled"); return c==0x0C11?scissorOn:(c==0x0B71?depthTest:(c==0x8037?polyOff:(c==0x0BE2?blendOn:0)));}
void Enable(G c){C("Enable"); if(c==0x0C11)scissorOn=1; if(c==0x0B71)depthTest=1; if(c==0x8037)polyOff=1; if(c==0x0BE2)blendOn=1;}
void Disable(G c){C("Disable"); if(c==0x0C11)scissorOn=0; if(c==0x0B71)depthTest=0; if(c==0x8037)polyOff=0; if(c==0x0BE2)blendOn=0;}
G GetError(){return 0;}
void ActiveTexture(G t){C("ActiveTexture");active=t-0x84C0;} void BindTexture(G,unsigned t){C("BindTexture");bound[active]=t;}
void GetTexLevelParameteriv(G,int,G p,int*v){C("GetTexLevelParameteriv"); *v = p==0x1000?2400:(p==0x1001?1200:0x8058);}
void GetRenderbufferParameteriv(G,G,int*v){C("GetRbParam");*v=0;} void BindRenderbuffer(G,unsigned r){C("BindRenderbuffer");rb=r;}
void GetFramebufferAttachmentParameteriv(G,G,G p,int*v){C("GetFbAttach"); *v = p==0x8CD0?0x1702:(p==0x8CD1?7:0);}
G CheckFramebufferStatus(G){C("CheckFramebufferStatus");return fboComplete?0x8CD5:0x8CD6;}
void GenTextures(int n,unsigned*t){C("GenTextures");for(int i=0;i<n;i++)t[i]=nextTex++;} void DeleteTextures(int,const unsigned*){C("DeleteTextures");}
void TexParameteri(G,G,int){C("TexParameteri");} void TexImage2D(G,int,int,int,int,int,G,G,const void*){C("TexImage2D");} void TexStorage2D(G,int,G f,int,int){C("TexStorage2D"); if(f==0x81A5||f==0x81A6||f==0x8CAC) lastDepthFormat=f;}
void GenFramebuffers(int n,unsigned*f){C("GenFramebuffers");for(int i=0;i<n;i++)f[i]=nextFbo++;}
void FramebufferTexture2D(G,G,G,unsigned,int){C("FramebufferTexture2D");}
void BindFramebuffer(G t,unsigned f){C("BindFramebuffer"); if(t==0x8CA8||t==0x8D40)readFbo=f; if(t==0x8CA9||t==0x8D40)drawFbo=f;}
void BlitFramebuffer(int,int,int,int,int,int,int,int,unsigned,G){C("BlitFramebuffer");}
int GetUniformLocation(unsigned p,const char*n){C("GetUniformLocation");
  if(p==7){ if(!strcmp(n,"ShadowPass")) return 500; if(!strcmp(n,"s_ShadowMap")) return 501; if(!strcmp(n,"ShadowRes")) return 502; return -1; }
  if(p==8){ if(!strcmp(n,"ShadowTransparentPass")) return 510; if(!strcmp(n,"ShadowRes")) return 502; return -1; }   // transparent caster
  if(p==10){ if(!strcmp(n,"ShadowPass")) return 530; if(!strcmp(n,"ShadowRes")) return 502; if(!strcmp(n,"ShadowCull")) return 531; if(!strcmp(n,"GameTimeOfDay")) return 532; if(!strcmp(n,"u_model")) return 533; return -1; }   // terrain caster with culling
  if(p==9){ if(!strcmp(n,"s_ShadowMap")) return 501; if(!strcmp(n,"s_ShadowAll")) return 511; if(!strcmp(n,"s_ShadowColor")) return 512; return -1; }   // receiver
  int k=!strcmp(n,"s_ScreenBuffer")?0:(!strcmp(n,"s_SceneDepthBuffer")?1:-1); return (k>=0&&progUnit[p][k]>=0)?(int)(p*2+k):-1;}
float settingsX=0.0f, settingsY=0.0f;
float modelM[16]; float timeOfDay=0.0f; float cullZ=1.0f;
void GetUniformfv(unsigned,int loc,float*v){C("GetUniformfv");
  if(loc==533){ for(int i=0;i<16;i++) v[i]=modelM[i]; return; }
  if(loc==531){ v[0]=64.0f; v[1]=400.0f; v[2]=cullZ; v[3]=0.0f; return; }
  if(loc==532){ v[0]=timeOfDay; v[1]=v[2]=v[3]=0.0f; return; }
  v[0]=(loc==502)?settingsX:0.0f; v[1]=(loc==502)?settingsY:0.0f; v[2]=v[3]=0.0f;}   // ShadowRes: x = size, y = depth bits
void GetUniformiv(unsigned p,int loc,int*v){C("GetUniformiv"); if(loc==501){*v=4;return;} if(loc==511){*v=5;return;} if(loc==512){*v=6;return;} *v=progUnit[p][loc-(int)(p*2)];}
void DrawElements(G,int,G,const void*){C("DrawElements"); drawSeenUnit3=bound[3]; if(shadowPassVal){++shadowDraws; castFbos.push_back(drawFbo); shadowDrawFbo=drawFbo; shadowDrawVp=vp[2]; shadowTexAtDraw=bound[4];}}
void Uniform4f(int loc,float x,float,float,float){C("Uniform4f"); if(loc==500||loc==510||loc==530) shadowPassVal=(int)x;}
void Viewport(int a,int b,int c,int d){C("Viewport"); vp[0]=a;vp[1]=b;vp[2]=c;vp[3]=d;}
void Clear(unsigned){C("Clear");} void ClearDepthf(float){C("ClearDepthf");} void DepthMask(unsigned char m){C("DepthMask"); depthMask=m;}
void DepthFunc(G f){C("DepthFunc"); depthFunc=f;} void DrawBuffers(int,const G*){C("DrawBuffers");} void PolygonOffset(float,float){C("PolygonOffset");}
void GetFloatv(G p,float*v){C("GetFloatv"); v[0]=1.0f; if(p==0x0C22){ v[1]=v[2]=v[3]=0.5f; } }   // only the colour clear value has 4 components
void ColorMask(unsigned char,unsigned char,unsigned char,unsigned char){C("ColorMask");} void ClearColor(float,float,float,float){C("ClearColor");} void GetBooleanv(G,unsigned char*v){C("GetBooleanv"); for(int i=0;i<4;i++) v[i]=(unsigned char)colorMaskOn;}
void ReadBuffer(G b){C("ReadBuffer");readBuffer=b;} void DrawArrays(G,int,int){} void DrawElementsInstanced(G,int,G,const void*,int){} void DrawArraysInstanced(G,int,int,int){}
void UseProgram(unsigned p){C("UseProgram");curProgram=p;} void LinkProgram(unsigned){} void DeleteProgram(unsigned){}
void DeleteFramebuffers(int,const unsigned*){} void DeleteRenderbuffers(int,const unsigned*){}
}
static int fails=0;
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL line %d: %s\n",__LINE__,#c); ++fails; } }while(0)
static void* fakeCtx=(void*)0x1234;
int main(){
  using namespace buffers;
  for(auto&p:fake::progUnit){p[0]=-1;p[1]=-1;}
  fake::progUnit[5][0]=3; fake::progUnit[6][0]=-1;
  Gl& r=real;
  r.BindFramebuffer=fake::BindFramebuffer; r.FramebufferTexture2D=fake::FramebufferTexture2D; r.DrawElements=fake::DrawElements; r.DrawArrays=fake::DrawArrays;
  r.DrawElementsInstanced=fake::DrawElementsInstanced; r.DrawArraysInstanced=fake::DrawArraysInstanced; r.GenTextures=fake::GenTextures; r.DeleteTextures=fake::DeleteTextures;
  r.BindTexture=fake::BindTexture; r.TexParameteri=fake::TexParameteri; r.TexImage2D=fake::TexImage2D; r.ReadBuffer=fake::ReadBuffer; r.TexStorage2D=fake::TexStorage2D; r.ActiveTexture=fake::ActiveTexture; r.GenFramebuffers=fake::GenFramebuffers;
  r.BlitFramebuffer=fake::BlitFramebuffer; r.CheckFramebufferStatus=fake::CheckFramebufferStatus; r.GetUniformLocation=fake::GetUniformLocation; r.GetUniformiv=fake::GetUniformiv; r.GetUniformfv=fake::GetUniformfv;
  r.GetIntegerv=fake::GetIntegerv; r.IsEnabled=fake::IsEnabled; r.Enable=fake::Enable; r.Disable=fake::Disable; r.GetError=fake::GetError;
  r.GetTexLevelParameteriv=fake::GetTexLevelParameteriv; r.GetRenderbufferParameteriv=fake::GetRenderbufferParameteriv; r.BindRenderbuffer=fake::BindRenderbuffer;
  r.GetFramebufferAttachmentParameteriv=fake::GetFramebufferAttachmentParameteriv; r.UseProgram=fake::UseProgram; r.LinkProgram=fake::LinkProgram; r.DeleteProgram=fake::DeleteProgram;
  r.DeleteFramebuffers=fake::DeleteFramebuffers; r.DeleteRenderbuffers=fake::DeleteRenderbuffers;
  r.GetBooleanv=fake::GetBooleanv; r.ColorMask=fake::ColorMask; r.ClearColor=fake::ClearColor; r.Uniform4f=fake::Uniform4f; r.Viewport=fake::Viewport; r.Clear=fake::Clear; r.ClearDepthf=fake::ClearDepthf; r.DepthMask=fake::DepthMask; r.DepthFunc=fake::DepthFunc; r.DrawBuffers=fake::DrawBuffers; r.PolygonOffset=fake::PolygonOffset; r.GetFloatv=fake::GetFloatv;
  gGetCurrentContext=[]()->void*{return fakeCtx;}; gActive=true;
  fake::drawFbo=2; fake::readFbo=2; fake::active=0;
  for(int i=0;i<32;i++)fake::bound[i]=0;

  // game: bind white at unit 3, use program 6 (no samplers)
  activeTextureDetour(0x84C0+3); bindTextureDetour(0x0DE1,77); activeTextureDetour(0x84C0+0);
  useProgramDetour(6);
  drawElementsDetour(4,6,0x1403,nullptr); // warm-up
  fake::calls.clear();
  for(int i=0;i<500;i++) drawElementsDetour(4,6,0x1403,nullptr);
  std::printf("plain program: %d GL gets over 500 draws (expect 0)\n",fake::calls["GetIntegerv"]);
  CHECK(fake::calls["GetIntegerv"]==0 && fake::calls["GetUniformLocation"]==0);

  // program using screen buffer
  useProgramDetour(5);
  fake::calls.clear();
  drawElementsDetour(4,6,0x1403,nullptr);
  CHECK(fake::drawSeenUnit3!=77 && fake::drawSeenUnit3>=100);     // our texture was bound during the draw
  CHECK(fake::bound[3]==77 && fake::active==0);                    // restored afterwards
  CHECK(fake::calls["BlitFramebuffer"]==1);
  std::printf("first capture draw: Check=%d FbAttach=%d Blit=%d\n",fake::calls["CheckFramebufferStatus"],fake::calls["GetFbAttach"],fake::calls["BlitFramebuffer"]);
  // steady state: next frames
  auto frameEnd=[&]{ State& st=state(); endFrame(st); };
  frameEnd(); fake::calls.clear();
  for(int f=0;f<10;f++){ for(int i=0;i<200;i++) drawElementsDetour(4,6,0x1403,nullptr); frameEnd(); }
  std::printf("steady state, 10 frames x200 draws: Check=%d FbAttach=%d TexLevel=%d Blit=%d GetInteger=%d\n",fake::calls["CheckFramebufferStatus"],fake::calls["GetFbAttach"],fake::calls["GetTexLevelParameteriv"],fake::calls["BlitFramebuffer"],fake::calls["GetIntegerv"]);
  CHECK(fake::calls["CheckFramebufferStatus"]==0 && fake::calls["GetFbAttach"]==0 && fake::calls["BlitFramebuffer"]==10);
  CHECK(fake::bound[3]==77 && fake::active==0);

  // teardown: framebuffer deleted -> epoch bump -> re-probe; incomplete FBO must skip the blit
  fake::fboComplete=false; unsigned f=2; deleteFramebuffersDetour(1,&f);
  fake::calls.clear(); frameEnd(); drawElementsDetour(4,6,0x1403,nullptr);
  CHECK(fake::calls["BlitFramebuffer"]==0 && fake::calls["CheckFramebufferStatus"]==1);
  CHECK(fake::drawSeenUnit3!=77 && fake::drawSeenUnit3>=100);       // no copy: our 1x1 fallback, never the game's own texture
  CHECK(fake::bound[3]==77 && fake::active==0);                      // and the game's binding is restored afterwards
  fake::fboComplete=true; deleteFramebuffersDetour(1,&f); frameEnd(); fake::calls.clear(); drawElementsDetour(4,6,0x1403,nullptr);
  CHECK(fake::calls["BlitFramebuffer"]==1);

  // read buffer of a framebuffer object is NONE (the thumbnail pass): copy anyway, with the read buffer forced and restored
  fake::readBuffer=0; deleteFramebuffersDetour(1,&f); frameEnd(); fake::calls.clear(); drawElementsDetour(4,6,0x1403,nullptr);
  CHECK(fake::calls["BlitFramebuffer"]==1 && fake::calls["ReadBuffer"]==2 && fake::readBuffer==0); fake::readBuffer=0x8CE0;
  // default framebuffer whose read buffer is not BACK -> still skipped
  fake::drawFbo=0; fake::readFbo=0; fake::readBuffer=0; gGetCurrentSurface=[](int)->void*{return (void*)1;}; frameEnd(); fake::calls.clear(); drawElementsDetour(4,6,0x1403,nullptr);
  CHECK(fake::calls["BlitFramebuffer"]==0); fake::drawFbo=2; fake::readFbo=2; fake::readBuffer=0x8CE0; gGetCurrentSurface=nullptr;
  // default framebuffer without a surface -> skip
  fake::drawFbo=0; gGetCurrentSurface=[](int)->void*{return nullptr;}; frameEnd(); fake::calls.clear(); drawElementsDetour(4,6,0x1403,nullptr);
  CHECK(fake::calls["BlitFramebuffer"]==0); fake::drawFbo=2; gGetCurrentSurface=nullptr;

  // relink moves the sampler to unit 4
  fake::progUnit[5][0]=4; fake::bound[4]=88; frameEnd();
  linkProgramDetour(5); fake::calls.clear(); drawElementsDetour(4,6,0x1403,nullptr);
  CHECK(fake::calls["BlitFramebuffer"]==1 && fake::bound[4]==88);

  // a missing UseProgram hook is detected and tracking falls back to queries
  fake::curProgram=6; // game switched program behind our back
  for(int i=0;i<2100;i++) drawElementsDetour(4,6,0x1403,nullptr);
  CHECK(state().tr.bad);
  fake::curProgram=5; fake::progUnit[5][0]=4; fake::calls.clear(); frameEnd(); drawElementsDetour(4,6,0x1403,nullptr);
  CHECK(fake::calls["BlitFramebuffer"]==1);
  { state().tr=Track{}; fake::curProgram=5; fake::active=0; fake::bound[3]=77; fake::drawFbo=2; fake::readFbo=2; useProgramDetour(5); activeTextureDetour(0x84C0); bindTextureDetour(0x0DE1,0); activeTextureDetour(0x84C0+3); bindTextureDetour(0x0DE1,77); activeTextureDetour(0x84C0); fake::progUnit[5][0]=3; fake::bound[4]=0; deleteFramebuffersDetour(1,&f); fake::calls.clear();
    for(int fr=0;fr<10;fr++){ for(int i=0;i<200;i++) drawElementsDetour(4,6,0x1403,nullptr); frameEnd(); }
    int tot=0; for(auto&kv:fake::calls) tot+=kv.second;
    std::printf("AFTER sampler program, 10 frames x200 draws: total GL calls=%d (GetIntegerv=%d)\n",tot,fake::calls["GetIntegerv"]); }

  // ---------------- shadow pass ----------------
  { state().sh=Shadow{}; state().tr=Track{}; state().unitsOf.clear(); state().lastUnits=nullptr; state().lastProgram=0;
    fake::drawFbo=2; fake::readFbo=2; fake::active=0; fake::bound[4]=66; fake::vp[2]=2400;
    fake::depthFunc=0x0203; fake::depthMask=0; fake::scissorOn=1; fake::depthTest=1; fake::polyOff=0;
    useProgramDetour(7); fake::calls.clear(); fake::shadowDraws=0;
    unsigned fr0=state().frame;
    drawElementsDetour(4,6,0x1403,nullptr);   // first caster draw of the frame
    CHECK(fake::shadowDraws==3);                                   // drawn a second time with ShadowPass=1
    CHECK(fake::shadowDrawFbo!=2 && fake::shadowDrawFbo>=10);      // into our own framebuffer
    CHECK(fake::shadowDrawVp==kShadowSize);                        // at shadow resolution
    CHECK(fake::shadowPassVal==0);                                 // ShadowPass back to 0
    CHECK(fake::drawFbo==2 && fake::vp[2]==2400);                  // FBO + viewport restored
    CHECK(fake::depthFunc==0x0203 && fake::depthMask==0 && fake::scissorOn==1 && fake::depthTest==1 && fake::polyOff==0); // state restored
    CHECK(fake::bound[4]==66 && fake::active==0);                  // game's texture restored
    CHECK(fake::calls["Clear"]==1);
    drawElementsDetour(4,6,0x1403,nullptr);   // second draw, same frame: no second clear
    CHECK(fake::calls["Clear"]==1 && fake::shadowDraws==6);
    CHECK(!state().sh.readValid);                                  // first frame: receivers still get the white fallback
    const int w0=state().sh.write; unsigned tex0=state().sh.tex[w0];
    { State& st=state(); endFrame(st); }
    CHECK(state().sh.readValid && state().sh.write==(w0^1) && state().frame==fr0+1);
    drawElementsDetour(4,6,0x1403,nullptr);   // next frame: receivers read last frame's map, casters write the other one
    CHECK(fake::calls["Clear"]==2);
    CHECK(state().sh.tex[state().sh.write]!=tex0);
    CHECK(fake::bound[4]==66);
    // program without ShadowPass is never cast
    useProgramDetour(6); fake::shadowDraws=0; drawElementsDetour(4,6,0x1403,nullptr); CHECK(fake::shadowDraws==0);
    // resolution comes from the material's ShadowRes.x and is applied once per frame
    { useProgramDetour(7); fake::settingsX=1024.0f; { State& st=state(); endFrame(st); } fake::shadowDraws=0;
      drawElementsDetour(4,6,0x1403,nullptr);
      CHECK(state().sh.size==1024 && fake::shadowDrawVp==1024 && fake::shadowDraws==3);   // maps recreated at the new size
      CHECK(!state().sh.readValid);                                                       // old-size map is never read
      fake::settingsX=99999.0f; { State& st=state(); endFrame(st); }
      drawElementsDetour(4,6,0x1403,nullptr);
      CHECK(state().sh.size==kShadowSizeMax && fake::shadowDrawVp==kShadowSizeMax);       // clamped
      fake::settingsX=0.0f; { State& st=state(); endFrame(st); }
      drawElementsDetour(4,6,0x1403,nullptr);
      CHECK(state().sh.size==kShadowSize);                                                // unset -> default
      fake::calls.clear(); drawElementsDetour(4,6,0x1403,nullptr);
      CHECK(fake::calls["GetUniformfv"]==0);                                              // only read once per frame
    }
    std::printf("shadow pass checks done\n"); }

  // ---------------- transparent casters (all-casters map + colour) ----------------
  { state().sh=Shadow{}; state().tr=Track{}; state().unitsOf.clear(); state().lastUnits=nullptr; state().lastProgram=0;
    fake::drawFbo=2; fake::readFbo=3; fake::active=0; fake::bound[4]=66; fake::bound[5]=55; fake::bound[6]=44; fake::vp[2]=2400;
    fake::blendOn=1; fake::colorMaskOn=0; fake::calls.clear(); fake::shadowDraws=0; fake::castFbos.clear();
    useProgramDetour(8);
    drawElementsDetour(4,6,0x1403,nullptr);   // first transparent caster of the frame
    State& st=state();
    CHECK(fake::shadowDraws==3 && fake::castFbos.size()==3);                       // one extra draw, no opaque one
    CHECK(fake::castFbos[0]==(int)st.sh.allFbo[st.sh.write]);                      // into the all-casters framebuffer
    CHECK(fake::calls["BlitFramebuffer"]==1);                                      // opaque depth copied into it
    CHECK(fake::calls["Clear"]==2);                                                // opaque depth cleared + colour cleared
    CHECK(fake::shadowPassVal==0);
    CHECK(fake::drawFbo==2 && fake::readFbo==3 && fake::vp[2]==2400);              // draw/read framebuffer + viewport restored
    CHECK(fake::blendOn==1);                                                       // game's blending restored
    drawElementsDetour(4,6,0x1403,nullptr);   // second transparent caster, same frame
    CHECK(fake::calls["BlitFramebuffer"]==1 && fake::calls["Clear"]==2 && fake::shadowDraws==6);   // map started only once
    // an opaque caster after the first transparent one is added to both maps
    fake::shadowDraws=0; fake::castFbos.clear(); useProgramDetour(7); drawElementsDetour(4,6,0x1403,nullptr);
    CHECK(fake::shadowDraws==6 && fake::castFbos[0]==(int)st.sh.fbo[st.sh.write] && fake::castFbos[3]==(int)st.sh.allFbo[st.sh.write]);
    CHECK(fake::blendOn==1);
    // next frame: opaque caster first -> only the opaque map until a transparent one shows up
    { endFrame(st); }
    CHECK(st.sh.readValid && st.sh.allReadValid);
    fake::shadowDraws=0; fake::castFbos.clear(); drawElementsDetour(4,6,0x1403,nullptr);
    CHECK(fake::shadowDraws==3 && fake::castFbos[0]==(int)st.sh.fbo[st.sh.write]);
    // receiver program: reads last frame's three maps, game's bindings are restored
    const int rd=st.sh.write^1; (void)rd;
    useProgramDetour(9); fake::calls.clear(); drawElementsDetour(4,6,0x1403,nullptr);
    CHECK(fake::bound[4]==66 && fake::bound[5]==55 && fake::bound[6]==44 && fake::active==0);
    // a frame without transparent casters: the all-casters maps must not be handed out (stale data)
    { endFrame(st); }
    CHECK(st.sh.readValid && !st.sh.allReadValid);
    { fake::calls.clear(); fake::bound[5]=55; useProgramDetour(9);
      int seen=-1; (void)seen;
      activeTextureDetour(0x84C0+5); bindTextureDetour(0x0DE1,55); activeTextureDetour(0x84C0);
      drawElementsDetour(4,6,0x1403,nullptr);
      CHECK(fake::bound[5]==55 && fake::active==0); }   // game's binding restored; unit 5 held the opaque map during the draw
    std::printf("transparent caster checks done\n"); }


  // ---------------- depth format (ShadowRes.y) ----------------
  { state().sh=Shadow{}; state().tr=Track{}; state().unitsOf.clear(); state().lastUnits=nullptr; state().lastProgram=0;
    fake::drawFbo=2; fake::readFbo=2; fake::settingsX=1024.0f;
    useProgramDetour(7);
    fake::settingsY=0.0f; drawElementsDetour(4,6,0x1403,nullptr);
    CHECK(state().sh.format==0x81A5 && fake::lastDepthFormat==0x81A5);            // unset -> default DEPTH_COMPONENT16
    fake::lastDepthFormat=0; { State& st=state(); endFrame(st); } drawElementsDetour(4,6,0x1403,nullptr);
    CHECK(fake::lastDepthFormat==0);                                              // unchanged: no recreation
    fake::settingsY=24.0f; { State& st=state(); endFrame(st); } drawElementsDetour(4,6,0x1403,nullptr);
    CHECK(state().sh.format==0x81A6 && fake::lastDepthFormat==0x81A6 && state().sh.size==1024);   // 24 bit, size kept
    fake::settingsY=32.0f; { State& st=state(); endFrame(st); } drawElementsDetour(4,6,0x1403,nullptr);
    CHECK(state().sh.format==0x8CAC);                                             // 32 bit float
    // transparent caster after a format change: all-casters map uses the same format as the opaque one
    fake::lastDepthFormat=0; useProgramDetour(8); { State& st=state(); endFrame(st); } drawElementsDetour(4,6,0x1403,nullptr);
    CHECK(fake::lastDepthFormat==0x8CAC);
    fake::settingsY=0.0f; fake::settingsX=0.0f;
    std::printf("depth format checks done\n"); }
  // ---------------- cost of one caster draw (GL calls issued on top of the game's own draw) ----------------
  { state().sh=Shadow{}; state().tr=Track{}; state().unitsOf.clear(); state().lastUnits=nullptr; state().lastProgram=0;
    fake::drawFbo=2; fake::readFbo=2; fake::active=0; fake::bound[4]=66;
    // what the game typically has during chunk draws: depth test + writes on, LEQUAL, no scissor, no polygon offset
    fake::scissorOn=0; fake::depthTest=1; fake::depthMask=1; fake::depthFunc=0x0203; fake::polyOff=0; fake::vp[2]=2400;
    auto perDraw=[&](int prog,const char* label){
      useProgramDetour(prog); drawElementsDetour(4,6,0x1403,nullptr);      // warm up (creates the maps)
      State& st=state(); endFrame(st); drawElementsDetour(4,6,0x1403,nullptr);
      fake::calls.clear(); for(int i=0;i<100;i++) drawElementsDetour(4,6,0x1403,nullptr);
      int tot=0,gets=0; for(auto&kv:fake::calls){ tot+=kv.second; if(kv.first.rfind("Get",0)==0||kv.first=="IsEnabled") gets+=kv.second; }
      std::printf("%-18s %5.1f GL calls per draw (%4.1f of them state queries)\n",label,tot/100.0,gets/100.0); };
    perDraw(7,"opaque caster");
    perDraw(8,"transparent caster");
    perDraw(9,"receiver only"); }
  // ---------------- chunk culling (ShadowCull) ----------------
  { state().sh=Shadow{}; state().tr=Track{}; state().unitsOf.clear(); state().lastUnits=nullptr; state().lastProgram=0;
    fake::drawFbo=2; fake::readFbo=2; fake::active=0; fake::vp[2]=2400; fake::depthFunc=0x0203; fake::depthMask=0; fake::scissorOn=1; fake::depthTest=1; fake::polyOff=0;
    fake::timeOfDay=0.0f; fake::cullZ=1.0f;   // sun straight up: light-space right = z, up = x
    auto setT=[&](float x,float y,float z,bool layoutA){ for(int i=0;i<16;i++) fake::modelM[i]=0.0f; fake::modelM[0]=fake::modelM[5]=fake::modelM[10]=fake::modelM[15]=1.0f;
      if(layoutA){ fake::modelM[12]=x; fake::modelM[13]=y; fake::modelM[14]=z; } else { fake::modelM[3]=x; fake::modelM[7]=y; fake::modelM[11]=z; } };
    useProgramDetour(10);
    auto drawn=[&]{ fake::shadowDraws=0; drawElementsDetour(4,6,0x1403,nullptr); return fake::shadowDraws; };
    setT(0,0,0,true);        CHECK(drawn()==3);   // chunk at the camera is cast
    setT(32,-16,-32,true);   CHECK(drawn()==3);   // inside the 64 block map
    setT(0,0,200,true);      CHECK(drawn()==0);   // far outside in light-space "right"
    setT(-200,0,0,true);     CHECK(drawn()==0);   // far outside in light-space "up"
    setT(0,300,0,true);      CHECK(drawn()==3);   // far above only: stays (height is along the sun, handled by depth clipping)
    setT(0,0,200,false);     CHECK(drawn()==0);   // other matrix layout, same result
    setT(0,0,0,false);       CHECK(drawn()==3);
    fake::modelM[0]=0.0625f; setT(0,0,200,true); fake::modelM[0]=0.0625f; CHECK(drawn()==3);   // scaled matrix: not understood, never culled
    fake::modelM[0]=1.0f; fake::modelM[12]=0; fake::modelM[13]=0; fake::modelM[14]=200; fake::modelM[3]=5.0f; CHECK(drawn()==3);   // both layouts nonzero: never culled
    { State& st=state(); endFrame(st); }
    fake::cullZ=0.0f; setT(0,0,200,true); CHECK(drawn()==3);   // z = 0 turns culling off (new frame re-reads the uniform)
    // an instanced draw is never culled
    fake::cullZ=1.0f; { State& st=state(); endFrame(st); } setT(0,0,200,true);
    fake::shadowDraws=0; fake::calls.clear(); drawElementsInstancedDetour(4,6,0x1403,nullptr,4); CHECK(fake::calls["GetUniformfv"]<=1);
    // per frame: ShadowCull + GameTimeOfDay are read once, only the model matrix once per draw
    { State& st=state(); endFrame(st); } fake::calls.clear(); setT(0,0,200,true);
    for(int i=0;i<50;i++) drawn();
    std::printf("culled chunk draws: %d GetUniformfv over 50 draws (expect 52)\n",fake::calls["GetUniformfv"]);
    CHECK(fake::calls["GetUniformfv"]==52);
    std::printf("culling checks done\n"); }

  std::printf("%s (%d failures)\n",fails?"FAILED":"all checks passed",fails);
  return fails?1:0;
}

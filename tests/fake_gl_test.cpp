// Host-side check of the hook logic against a fake GL (no device needed):
//   g++ -std=c++20 -O1 -Iinclude -Ivendor tests/fake_gl_test.cpp -o /tmp/fake_gl_test -ldl && /tmp/fake_gl_test
#include "../src/GlHooks.cpp"
#include <cstdio>
#include <map>
#include <string>
namespace pl::memory { int hook(FuncPtr, FuncPtr, FuncPtr*, HookPriority) { return -1; } bool unhook(FuncPtr, FuncPtr) { return false; } }

using namespace buffers;
// ---------------- fake GL ----------------
namespace fake {
using G = buffers::GLenum;
std::map<std::string,int> calls;
int curProgram=0, active=0, drawFbo=0, readFbo=0, rb=0, readBuffer=0x8CE0, nextTex=100, nextFbo=10;
int bound[32]; int viewport[4]={0,0,2400,1200};
int sceneTexSamples=0; bool fboComplete=true; int progUnit[16][2]; // [prog][kind]
int drawSeenUnit3=-1; int errorOnce=0;
#define C(n) ++calls[n]
void GetIntegerv(G p, int* v){ C("GetIntegerv");
  switch(p){case 0x8B8D:*v=curProgram;break;case 0x84E0:*v=0x84C0+active;break;case 0x8069:*v=bound[active];break;
  case 0x8CA6:*v=drawFbo;break;case 0x8CAA:*v=readFbo;break;case 0x8CA7:*v=rb;break;case 0x0BA2:for(int i=0;i<4;i++)v[i]=viewport[i];break;
  case 0x0C02:*v=readBuffer;break;case 0x80A9:*v=0;break;default:*v=0;} }
unsigned char IsEnabled(G){C("IsEnabled");return 0;} void Enable(G){C("Enable");} void Disable(G){C("Disable");}
G GetError(){return 0;}
void ActiveTexture(G t){C("ActiveTexture");active=t-0x84C0;} void BindTexture(G,unsigned t){C("BindTexture");bound[active]=t;}
void GetTexLevelParameteriv(G,int,G p,int*v){C("GetTexLevelParameteriv"); *v = p==0x1000?2400:(p==0x1001?1200:0x8058);}
void GetRenderbufferParameteriv(G,G,int*v){C("GetRbParam");*v=0;} void BindRenderbuffer(G,unsigned r){C("BindRenderbuffer");rb=r;}
void GetFramebufferAttachmentParameteriv(G,G,G p,int*v){C("GetFbAttach"); *v = p==0x8CD0?0x1702:(p==0x8CD1?7:0);}
G CheckFramebufferStatus(G){C("CheckFramebufferStatus");return fboComplete?0x8CD5:0x8CD6;}
void GenTextures(int n,unsigned*t){C("GenTextures");for(int i=0;i<n;i++)t[i]=nextTex++;} void DeleteTextures(int,const unsigned*){C("DeleteTextures");}
void TexParameteri(G,G,int){C("TexParameteri");} void TexImage2D(G,int,int,int,int,int,G,G,const void*){C("TexImage2D");} void TexStorage2D(G,int,G,int,int){C("TexStorage2D");}
void GenFramebuffers(int n,unsigned*f){C("GenFramebuffers");for(int i=0;i<n;i++)f[i]=nextFbo++;}
void FramebufferTexture2D(G,G,G,unsigned,int){C("FramebufferTexture2D");}
void BindFramebuffer(G t,unsigned f){C("BindFramebuffer"); if(t==0x8CA8||t==0x8D40)readFbo=f; if(t==0x8CA9||t==0x8D40)drawFbo=f;}
void BlitFramebuffer(int,int,int,int,int,int,int,int,unsigned,G){C("BlitFramebuffer");}
int GetUniformLocation(unsigned p,const char*n){C("GetUniformLocation"); int k=!strcmp(n,"s_ScreenBuffer")?0:(!strcmp(n,"s_SceneDepthBuffer")?1:-1); return (k>=0&&progUnit[p][k]>=0)?(int)(p*2+k):-1;}
void GetUniformiv(unsigned p,int loc,int*v){C("GetUniformiv"); *v=progUnit[p][loc-(int)(p*2)];}
void DrawElements(G,int,G,const void*){C("DrawElements"); drawSeenUnit3=bound[3];}
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
  r.BlitFramebuffer=fake::BlitFramebuffer; r.CheckFramebufferStatus=fake::CheckFramebufferStatus; r.GetUniformLocation=fake::GetUniformLocation; r.GetUniformiv=fake::GetUniformiv;
  r.GetIntegerv=fake::GetIntegerv; r.IsEnabled=fake::IsEnabled; r.Enable=fake::Enable; r.Disable=fake::Disable; r.GetError=fake::GetError;
  r.GetTexLevelParameteriv=fake::GetTexLevelParameteriv; r.GetRenderbufferParameteriv=fake::GetRenderbufferParameteriv; r.BindRenderbuffer=fake::BindRenderbuffer;
  r.GetFramebufferAttachmentParameteriv=fake::GetFramebufferAttachmentParameteriv; r.UseProgram=fake::UseProgram; r.LinkProgram=fake::LinkProgram; r.DeleteProgram=fake::DeleteProgram;
  r.DeleteFramebuffers=fake::DeleteFramebuffers; r.DeleteRenderbuffers=fake::DeleteRenderbuffers;
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
  std::printf("%s (%d failures)\n",fails?"FAILED":"all checks passed",fails);
  return fails?1:0;
}

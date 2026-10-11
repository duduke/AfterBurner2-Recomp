// Optional macOS development check against the unmodified upstream GLSL.
// Synthetic inputs only; no ROM assets or game state enter this check.
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#include <SDL3/SDL.h>
#include "frontend/sprite_filter.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
void require(bool v,const char* m){if(!v)throw std::runtime_error(m);}
GLuint compile(GLenum kind,const std::string& source){
 const auto shader=glCreateShader(kind);const char* p=source.c_str();glShaderSource(shader,1,&p,nullptr);glCompileShader(shader);
 GLint success=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&success);
 if(!success){char log[8192];glGetShaderInfoLog(shader,sizeof(log),nullptr,log);throw std::runtime_error(log);}return shader;
}
std::vector<uint32_t> reference(std::span<const uint32_t> input,int w,int h,bool alpha_metric=false){
 std::vector<float> rgba;rgba.reserve(input.size()*4);
 for(auto p:input){rgba.push_back(((p>>16)&255)/255.f);rgba.push_back(((p>>8)&255)/255.f);rgba.push_back((p&255)/255.f);rgba.push_back((p>>24)/255.f);}
 GLuint textures[6],fbo,vao;glGenTextures(6,textures);glGenFramebuffers(1,&fbo);glGenVertexArrays(1,&vao);glBindVertexArray(vao);
 for(int i=0;i<6;++i){glBindTexture(GL_TEXTURE_2D,textures[i]);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
 glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,i==5?w*3:w,i==5?h*3:h,0,GL_RGBA,GL_FLOAT,i==0?rgba.data():nullptr);}
 const auto vertex=compile(GL_VERTEX_SHADER,"#version 150\nout vec4 TEX0; void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2); TEX0=vec4(p,0,1);gl_Position=vec4(p*2.-1.,0,1);}");
 for(int i=0;i<5;++i){
  std::ifstream file("src/main/frontend/scalers/scalefx-pass"+std::to_string(i)+".glsl");require(bool(file),"missing upstream shader");
  std::string source((std::istreambuf_iterator<char>(file)),{});source.replace(0,source.find('\n'),"#version 150\n#define FRAGMENT");
  if(i==0 && alpha_metric) {
   const auto replace=[&](const std::string& a,const std::string& b){size_t n=0;while((n=source.find(a,n))!=std::string::npos){source.replace(n,a.size(),b);n+=b.size();}};
   replace("float dist(vec3 A, vec3 B)","float dist(vec4 A, vec4 B)");
   replace("float r = 0.5", "if (A.a != B.a) return 1.; float r = 0.5");
   replace("vec3 d = A - B;", "vec3 d = A.rgb - B.rgb;");
   replace(").rgb", ")");
   replace("vec3 A =", "vec4 A =");replace("vec3 B =", "vec4 B =");replace("vec3 C =", "vec4 C =");replace("vec3 E =", "vec4 E =");replace("vec3 F =", "vec4 F =");
  }
  const auto fragment=compile(GL_FRAGMENT_SHADER,source),program=glCreateProgram();glAttachShader(program,vertex);glAttachShader(program,fragment);glLinkProgram(program);
  GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);require(linked,"shader link failed");glUseProgram(program);
  glUniform2f(glGetUniformLocation(program,"TextureSize"),w,h);glUniform2f(glGetUniformLocation(program,"InputSize"),w,h);
  glUniform2f(glGetUniformLocation(program,"OutputSize"),i==4?w*3:w,i==4?h*3:h);
  for(const auto& binding: {std::pair{"Texture",i},std::pair{"PassPrev2Texture",1},std::pair{"PassPrev5Texture",0}}){
   const int unit=binding.first==std::string("Texture")?0:binding.first==std::string("PassPrev2Texture")?1:2;
   glActiveTexture(GL_TEXTURE0+unit);glBindTexture(GL_TEXTURE_2D,textures[binding.second]);glUniform1i(glGetUniformLocation(program,binding.first),unit);
  }
  glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[i+1],0);
  require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"float framebuffer unavailable");glViewport(0,0,i==4?w*3:w,i==4?h*3:h);glDrawArrays(GL_TRIANGLES,0,3);
  glDeleteProgram(program);glDeleteShader(fragment);
 }
 rgba.resize(w*h*9*4);glReadPixels(0,0,w*3,h*3,GL_RGBA,GL_FLOAT,rgba.data());require(glGetError()==GL_NO_ERROR,"GL error");
 std::vector<uint32_t> result(w*h*9);
 for(size_t i=0;i<result.size();++i)result[i]=uint32_t(std::lround(rgba[i*4+3]*255))<<24|uint32_t(std::lround(rgba[i*4]*255))<<16|uint32_t(std::lround(rgba[i*4+1]*255))<<8|uint32_t(std::lround(rgba[i*4+2]*255));
 glDeleteShader(vertex);glDeleteTextures(6,textures);glDeleteFramebuffers(1,&fbo);glDeleteVertexArrays(1,&vao);return result;
}
int main()try{
 require(SDL_Init(SDL_INIT_VIDEO),SDL_GetError());SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,2);SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
 auto* window=SDL_CreateWindow("ScaleFX reference",64,64,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);require(window,SDL_GetError());auto context=SDL_GL_CreateContext(window);require(context,SDL_GetError());
 Frontend::SpriteScaler scaler;const int w=32,h=24;std::vector<uint32_t> bg(w*h,0xff000000),pixels(w*h);
 for(int pattern=0;pattern<6;++pattern){
  uint32_t random=0x12345678;
  for(int y=0;y<h;++y)for(int x=0;x<w;++x){random=random*1664525+1013904223;const int k=pattern==0?x>y:pattern==1?((x/3+y/3)%2):pattern==2?(x==y || x+y==25):((random>>24)%5);
   constexpr uint32_t colors[]{0xff000000,0xffffffff,0xffa04080,0xff102080,0xff70cc44};pixels[y*w+x]=pattern<4?colors[k]:pattern==4?(x>y?colors[(x/3)%5]:0):((x+y)%4?colors[k]:0);}
  auto expected=reference(pixels,w,h,pattern>=4);
  for(auto& p:expected)if(!(p>>24))p=0xff000000;const auto actual=scaler.compose(bg,pixels,w,h,Frontend::SpriteFilter::scalefx);
  size_t differences=0;for(size_t i=0;i<actual.size();++i)differences+=actual[i]!=expected[i];
  uint64_t hash=14695981039346656037ull;for(auto p:expected){for(unsigned shift:{0u,8u,16u,24u}){hash^=(p>>shift)&255;hash*=1099511628211ull;}}
  std::cout<<"Expected FNV64 "<<std::hex<<hash<<std::dec<<"\n";
  std::cout<<"ScaleFX upstream GLSL pattern "<<pattern<<": "<<differences<<" differences / "<<actual.size()<<" pixels\n";
  require(!differences,"CPU ScaleFX differs from upstream GLSL");
 }
 SDL_GL_DestroyContext(context);SDL_DestroyWindow(window);SDL_Quit();return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

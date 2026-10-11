// ScaleFX CPU adaptation of Sp00kyFox's five-pass GLSL algorithm (2017-03-01).
// MIT license and upstream source: scalefx-pass0..4.glsl in this directory.
// Float intermediate planes, clamp-to-edge sampling; defaults CLR=.5f, SAA=1,
// SCN=1.f Sprite transparency adds a maximal metric at opaque/transparent edges.
/*
Copyright (c) 2016 Sp00kyFox - ScaleFX@web.de

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

*/
#pragma once
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <span>
#include <vector>
namespace Frontend::ScaleFX {
struct vec2 { float x{},y{}; vec2(float x,float y):x(x),y(y){} };
struct vec3 { float x{},y{},z{}; vec3(float x,float y,float z):x(x),y(y),z(z){} };
struct vec4 {
 float x{},y{},z{},w{};
 vec4()=default; explicit vec4(float v):x(v),y(v),z(v),w(v){}
 vec4(float x,float y,float z,float w):x(x),y(y),z(z),w(w){}
 vec3 yzw() const {return vec3(y,z,w);}
 vec3 zwx() const {return vec3(z,w,x);}
 vec3 wxy() const {return vec3(w,x,y);}
 vec3 xyz() const {return vec3(x,y,z);}
 vec4 yzwx() const {return vec4(y,z,w,x);}
 vec4 wxyz() const {return vec4(w,x,y,z);}
 vec4 zwxy() const {return vec4(z,w,x,y);}
};
using bvec2=vec2; using bvec4=vec4;
inline vec4 operator+(vec4 a,vec4 b) { return vec4(a.x+b.x,a.y+b.y,a.z+b.z,a.w+b.w); }
inline vec4 operator+(vec4 a,float b) { return vec4(a.x+b,a.y+b,a.z+b,a.w+b); }
inline vec4 operator+(float a,vec4 b) { return vec4(a+b.x,a+b.y,a+b.z,a+b.w); }
inline vec4 operator-(vec4 a,vec4 b) { return vec4(a.x-b.x,a.y-b.y,a.z-b.z,a.w-b.w); }
inline vec4 operator-(vec4 a,float b) { return vec4(a.x-b,a.y-b,a.z-b,a.w-b); }
inline vec4 operator-(float a,vec4 b) { return vec4(a-b.x,a-b.y,a-b.z,a-b.w); }
inline vec4 operator*(vec4 a,vec4 b) { return vec4(a.x*b.x,a.y*b.y,a.z*b.z,a.w*b.w); }
inline vec4 operator*(vec4 a,float b) { return vec4(a.x*b,a.y*b,a.z*b,a.w*b); }
inline vec4 operator*(float a,vec4 b) { return vec4(a*b.x,a*b.y,a*b.z,a*b.w); }
inline vec4 operator/(vec4 a,vec4 b) { return vec4(a.x/b.x,a.y/b.y,a.z/b.z,a.w/b.w); }
inline vec4 operator/(vec4 a,float b) { return vec4(a.x/b,a.y/b,a.z/b,a.w/b); }
inline vec4 operator/(float a,vec4 b) { return vec4(a/b.x,a/b.y,a/b.z,a/b.w); }
inline float min(float a,float b){return std::min(a,b);}
inline float max(float a,float b){return std::max(a,b);}
inline float clamp(float a,float b,float c){return std::clamp(a,b,c);}
inline vec4 min(vec4 a,vec4 b){return vec4(min(a.x,b.x),min(a.y,b.y),min(a.z,b.z),min(a.w,b.w));}
inline vec4 min(vec4 a,float b){return vec4(min(a.x,b),min(a.y,b),min(a.z,b),min(a.w,b));}
inline vec4 max(vec4 a,vec4 b){return vec4(max(a.x,b.x),max(a.y,b.y),max(a.z,b.z),max(a.w,b.w));}
inline vec4 max(vec4 a,float b){return vec4(max(a.x,b),max(a.y,b),max(a.z,b),max(a.w,b));}
inline vec4 step(vec4 a,vec4 b){return vec4((b.x<a.x?0.f:1.f),(b.y<a.y?0.f:1.f),(b.z<a.z?0.f:1.f),(b.w<a.w?0.f:1.f));}
inline vec4 step(vec4 a,float b){return vec4((b<a.x?0.f:1.f),(b<a.y?0.f:1.f),(b<a.z?0.f:1.f),(b<a.w?0.f:1.f));}
inline vec4 floor(vec4 a){return vec4(std::floor(a.x),std::floor(a.y),std::floor(a.z),std::floor(a.w));}
inline vec4 mod(vec4 a,float b){return vec4(a.x-b*std::floor(a.x/b),a.y-b*std::floor(a.y/b),a.z-b*std::floor(a.z/b),a.w-b*std::floor(a.w/b));}
inline vec4 LE(vec4 x,vec4 y){return 1.f-step(y,x);}
inline vec4 GE(vec4 x,vec4 y){return 1.f-step(x,y);}
inline float GE(float x,float y){return x>y?1.f:0.f;}
inline vec4 LEQ(vec4 x,vec4 y){return step(x,y);}
inline vec4 NOT(vec4 x){return 1.f-x;}
inline float NOT(float x){return 1.f-x;}
inline float dist(uint32_t a,uint32_t b){
 if ((a>>24)!=(b>>24)) return 1.f;
 const float ar=((a>>16)&255)/255.f,br=((b>>16)&255)/255.f;
 const float r=(ar+br)*.5f, dr=ar-br;
 const float dg=float(int((a>>8)&255)-int((b>>8)&255))/255.f;
 const float db=float(int(a&255)-int(b&255))/255.f;
 return std::sqrt((2.f+r)*dr*dr+4.f*dg*dg+(3.f-r)*db*db)/3.f;
}
inline int index(int x,int y,int w,int h){return std::clamp(y,0,h-1)*w+std::clamp(x,0,w-1);}
inline float str(float d,vec2 a,vec2 b){
 const float diff=a.x-a.y,wght1=max(.5f-d,0.f)/.5f;
 const float wght2=clamp((1.f-d)+(min(a.x,b.x)+a.x>min(a.y,b.y)+a.y?diff:-diff),0.f,1.f);
 return (wght1*wght2)*(a.x*a.y);
}
vec4 dom(vec3 x, vec3 y, vec3 z, vec3 w){
	return 2.f * vec4(x.y, y.y, z.y, w.y) - (vec4(x.x, y.x, z.x, w.x) + vec4(x.z, y.z, z.z, w.z));
}

// necessary but not sufficient junction condition for orthogonal edges
float clear(vec2 crn, vec2 a, vec2 b){
	return (crn.x >= max(min(a.x, a.y), min(b.x, b.y))) && (crn.y >= max(min(a.x, b.y), min(b.x, a.y))) ? 1.f : 0.f;
}

bvec4 loadCorn(vec4 x){
	return bvec4(floor(mod(x*15.f + 0.5f, 2.f)));
}

// extract second bool4 from float4 - horizontal edges
bvec4 loadHori(vec4 x){
	return bvec4(floor(mod(x*7.5f + 0.25f, 2.f)));
}

// extract third bool4 from float4 - vertical edges
bvec4 loadVert(vec4 x){
	return bvec4(floor(mod(x*3.75f + 0.125f, 2.f)));
}

// extract fourth bool4 from float4 - orientation
bvec4 loadOr(vec4 x){
	return bvec4(floor(mod(x*1.875f + 0.0625f, 2.f)));
}

inline void scale(std::span<const uint32_t> input,int w,int h,std::vector<uint32_t>& output,
                  std::vector<vec4>& metric,std::vector<vec4>& strength,std::vector<vec4>& flags,std::vector<vec4>& tags){
 const auto n=w*h;metric.resize(n);strength.resize(n);flags.resize(n);tags.resize(n);output.resize(n*9);
 for(int y=0;y<h;++y)for(int x=0;x<w;++x){
  const auto E=input[y*w+x];
  metric[y*w+x]=vec4(dist(E,input[index(x-1,y-1,w,h)]),dist(E,input[index(x,y-1,w,h)]),
                     dist(E,input[index(x+1,y-1,w,h)]),dist(E,input[index(x+1,y,w,h)]));
 }
 for(int y=0;y<h;++y)for(int x=0;x<w;++x){


	// metric data
	vec4 A = metric[index(x+(-1),y+(-1),w,h)], B = metric[index(x+(0),y+(-1),w,h)];
	vec4 D = metric[index(x+(-1),y+(0),w,h)], E = metric[index(x+(0),y+(0),w,h)], F = metric[index(x+(1),y+(0),w,h)];
	vec4 G = metric[index(x+(-1),y+(1),w,h)], H = metric[index(x+(0),y+(1),w,h)], I = metric[index(x+(1),y+(1),w,h)];
// corner strength
	vec4 res;
	res.x = str(D.z, vec2(D.w, E.y), vec2(A.w, D.y));
	res.y = str(F.x, vec2(E.w, E.y), vec2(B.w, F.y));
	res.z = str(H.z, vec2(E.w, H.y), vec2(H.w, I.y));
	res.w = str(H.x, vec2(D.w, H.y), vec2(G.w, G.y));

	strength[y*w+x] = res;
 }
 for(int y=0;y<h;++y)for(int x=0;x<w;++x){



	// metric data
	vec4 A = metric[index(x+(-1),y+(-1),w,h)], B = metric[index(x+(0),y+(-1),w,h)];
	vec4 D = metric[index(x+(-1),y+(0),w,h)], E = metric[index(x+(0),y+(0),w,h)], F = metric[index(x+(1),y+(0),w,h)];
	vec4 G = metric[index(x+(-1),y+(1),w,h)], H = metric[index(x+(0),y+(1),w,h)], I = metric[index(x+(1),y+(1),w,h)];

	// strength data
	vec4 As = strength[index(x+(-1),y+(-1),w,h)], Bs = strength[index(x+(0),y+(-1),w,h)], Cs = strength[index(x+(1),y+(-1),w,h)];
	vec4 Ds = strength[index(x+(-1),y+(0),w,h)], Es = strength[index(x+(0),y+(0),w,h)], Fs = strength[index(x+(1),y+(0),w,h)];
	vec4 Gs = strength[index(x+(-1),y+(1),w,h)], Hs = strength[index(x+(0),y+(1),w,h)], Is = strength[index(x+(1),y+(1),w,h)];
// strength & dominance junctions
	vec4 jSx = vec4(As.z, Bs.w, Es.x, Ds.y), jDx = dom(As.yzw(), Bs.zwx(), Es.wxy(), Ds.xyz());
	vec4 jSy = vec4(Bs.z, Cs.w, Fs.x, Es.y), jDy = dom(Bs.yzw(), Cs.zwx(), Fs.wxy(), Es.xyz());
	vec4 jSz = vec4(Es.z, Fs.w, Is.x, Hs.y), jDz = dom(Es.yzw(), Fs.zwx(), Is.wxy(), Hs.xyz());
	vec4 jSw = vec4(Ds.z, Es.w, Hs.x, Gs.y), jDw = dom(Ds.yzw(), Es.zwx(), Hs.wxy(), Gs.xyz());


	// majority vote for ambiguous dominance junctions
	vec4 zero4 = vec4(0.f);
	vec4 jx = min(GE(jDx, zero4) * (LEQ(jDx.yzwx(), zero4) * LEQ(jDx.wxyz(), zero4) + GE(jDx + jDx.zwxy(), jDx.yzwx() + jDx.wxyz())), 1.f);
	vec4 jy = min(GE(jDy, zero4) * (LEQ(jDy.yzwx(), zero4) * LEQ(jDy.wxyz(), zero4) + GE(jDy + jDy.zwxy(), jDy.yzwx() + jDy.wxyz())), 1.f);
	vec4 jz = min(GE(jDz, zero4) * (LEQ(jDz.yzwx(), zero4) * LEQ(jDz.wxyz(), zero4) + GE(jDz + jDz.zwxy(), jDz.yzwx() + jDz.wxyz())), 1.f);
	vec4 jw = min(GE(jDw, zero4) * (LEQ(jDw.yzwx(), zero4) * LEQ(jDw.wxyz(), zero4) + GE(jDw + jDw.zwxy(), jDw.yzwx() + jDw.wxyz())), 1.f);


	// inject strength without creating new contradictions
	vec4 res;
	res.x = min(jx.z + NOT(jx.y) * NOT(jx.w) * GE(jSx.z, 0.f) * (jx.x + GE(jSx.x + jSx.z, jSx.y + jSx.w)), 1.f);
	res.y = min(jy.w + NOT(jy.z) * NOT(jy.x) * GE(jSy.w, 0.f) * (jy.y + GE(jSy.y + jSy.w, jSy.x + jSy.z)), 1.f);
	res.z = min(jz.x + NOT(jz.w) * NOT(jz.y) * GE(jSz.x, 0.f) * (jz.z + GE(jSz.x + jSz.z, jSz.y + jSz.w)), 1.f);
	res.w = min(jw.y + NOT(jw.x) * NOT(jw.z) * GE(jSw.y, 0.f) * (jw.w + GE(jSw.y + jSw.w, jSw.x + jSw.z)), 1.f);


	// single pixel & end of line detection
	res = min(res * (vec4(jx.z, jy.w, jz.x, jw.y) + NOT(res.wxyz() * res.yzwx())), 1.f);


	// output

	vec4 clr;
	clr.x = clear(vec2(D.z, E.x), vec2(D.w, E.y), vec2(A.w, D.y));
	clr.y = clear(vec2(F.x, E.z), vec2(E.w, E.y), vec2(B.w, F.y));
	clr.z = clear(vec2(H.z, I.x), vec2(E.w, H.y), vec2(H.w, I.y));
	clr.w = clear(vec2(H.x, G.z), vec2(D.w, H.y), vec2(G.w, G.y));

	vec4 h = vec4(min(D.w, A.w), min(E.w, B.w), min(E.w, H.w), min(D.w, G.w));
	vec4 v = vec4(min(E.y, D.y), min(E.y, F.y), min(H.y, I.y), min(H.y, G.y));

	vec4 orientation   = GE(h + vec4(D.w, E.w, E.w, D.w), v + vec4(E.y, E.y, H.y, H.y));	// orientation
	vec4 hori = LE(h, v) * clr;	// horizontal edges
	vec4 vert = GE(h, v) * clr;	// vertical edges

	flags[y*w+x] = (res + 2.f * hori + 4.f * vert + 8.f * orientation) / 15.f;
 }
 for(int y=0;y<h;++y)for(int x=0;x<w;++x){


	// read data
	vec4 E = flags[index(x+(0),y+(0),w,h)];
	vec4 D = flags[index(x+(-1),y+(0),w,h)], D0 = flags[index(x+(-2),y+(0),w,h)], D1 = flags[index(x+(-3),y+(0),w,h)];
	vec4 F = flags[index(x+(1),y+(0),w,h)], F0 = flags[index(x+(2),y+(0),w,h)], F1 = flags[index(x+(3),y+(0),w,h)];
	vec4 B = flags[index(x+(0),y+(-1),w,h)], B0 = flags[index(x+(0),y+(-2),w,h)], B1 = flags[index(x+(0),y+(-3),w,h)];
	vec4 H = flags[index(x+(0),y+(1),w,h)], H0 = flags[index(x+(0),y+(2),w,h)], H1 = flags[index(x+(0),y+(3),w,h)];
// extract data
	bvec4 Ec = loadCorn(E), Eh = loadHori(E), Ev = loadVert(E), Eo = loadOr(E);
	bvec4 Dc = loadCorn(D),	Dh = loadHori(D), Do = loadOr(D), D0c = loadCorn(D0), D0h = loadHori(D0), D1h = loadHori(D1);
	bvec4 Fc = loadCorn(F),	Fh = loadHori(F), Fo = loadOr(F), F0c = loadCorn(F0), F0h = loadHori(F0), F1h = loadHori(F1);
	bvec4 Bc = loadCorn(B),	Bv = loadVert(B), Bo = loadOr(B), B0c = loadCorn(B0), B0v = loadVert(B0), B1v = loadVert(B1);
	bvec4 Hc = loadCorn(H),	Hv = loadVert(H), Ho = loadOr(H), H0c = loadCorn(H0), H0v = loadVert(H0), H1v = loadVert(H1);


	// lvl1 corners (hori, vert)
	bool lvl1x = Ec.x && (Dc.z || Bc.z || 1.0f == 1.f);
	bool lvl1y = Ec.y && (Fc.w || Bc.w || 1.0f == 1.f);
	bool lvl1z = Ec.z && (Fc.x || Hc.x || 1.0f == 1.f);
	bool lvl1w = Ec.w && (Dc.y || Hc.y || 1.0f == 1.f);

	// lvl2 mid (left, right / up, down)
	bvec2 lvl2x = bvec2(((Ec.x && Eh.y)) && Dc.z, ((Ec.y && Eh.x)) && Fc.w);
	bvec2 lvl2y = bvec2(((Ec.y && Ev.z)) && Bc.w, ((Ec.z && Ev.y)) && Hc.x);
	bvec2 lvl2z = bvec2(((Ec.w && Eh.z)) && Dc.y, ((Ec.z && Eh.w)) && Fc.x);
	bvec2 lvl2w = bvec2(((Ec.x && Ev.w)) && Bc.z, ((Ec.w && Ev.x)) && Hc.y);

	// lvl3 corners (hori, vert)
	bvec2 lvl3x = bvec2(lvl2x.y && ((Dh.y && Dh.x)) && Fh.z, lvl2w.y && ((Bv.w && Bv.x)) && Hv.z);
	bvec2 lvl3y = bvec2(lvl2x.x && ((Fh.x && Fh.y)) && Dh.w, lvl2y.y && ((Bv.z && Bv.y)) && Hv.w);
	bvec2 lvl3z = bvec2(lvl2z.x && ((Fh.w && Fh.z)) && Dh.x, lvl2y.x && ((Hv.y && Hv.z)) && Bv.x);
	bvec2 lvl3w = bvec2(lvl2z.y && ((Dh.z && Dh.w)) && Fh.y, lvl2w.x && ((Hv.x && Hv.w)) && Bv.y);

	// lvl4 corners (hori, vert)
	bvec2 lvl4x = bvec2(((Dc.x && Dh.y) && (Eh.x && Eh.y) && (Fh.x && Fh.y)) && ((D0c.z && D0h.w)), ((Bc.x && Bv.w) && (Ev.x && Ev.w) && (Hv.x && Hv.w)) && ((B0c.z && B0v.y)));
	bvec2 lvl4y = bvec2(((Fc.y && Fh.x) && (Eh.y && Eh.x) && (Dh.y && Dh.x)) && ((F0c.w && F0h.z)), ((Bc.y && Bv.z) && (Ev.y && Ev.z) && (Hv.y && Hv.z)) && ((B0c.w && B0v.x)));
	bvec2 lvl4z = bvec2(((Fc.z && Fh.w) && (Eh.z && Eh.w) && (Dh.z && Dh.w)) && ((F0c.x && F0h.y)), ((Hc.z && Hv.y) && (Ev.z && Ev.y) && (Bv.z && Bv.y)) && ((H0c.x && H0v.w)));
	bvec2 lvl4w = bvec2(((Dc.w && Dh.z) && (Eh.w && Eh.z) && (Fh.w && Fh.z)) && ((D0c.y && D0h.x)), ((Hc.w && Hv.x) && (Ev.w && Ev.x) && (Bv.w && Bv.x)) && ((H0c.y && H0v.z)));

	// lvl5 mid (left, right / up, down)
	bvec2 lvl5x = bvec2(lvl4x.x && ((F0h.x && F0h.y)) && ((D1h.z && D1h.w)), lvl4y.x && ((D0h.y && D0h.x)) && ((F1h.w && F1h.z)));
	bvec2 lvl5y = bvec2(lvl4y.y && ((H0v.y && H0v.z)) && ((B1v.w && B1v.x)), lvl4z.y && ((B0v.z && B0v.y)) && ((H1v.x && H1v.w)));
	bvec2 lvl5z = bvec2(lvl4w.x && ((F0h.w && F0h.z)) && ((D1h.y && D1h.x)), lvl4z.x && ((D0h.z && D0h.w)) && ((F1h.x && F1h.y)));
	bvec2 lvl5w = bvec2(lvl4x.y && ((H0v.x && H0v.w)) && ((B1v.z && B1v.y)), lvl4w.y && ((B0v.w && B0v.x)) && ((H1v.y && H1v.z)));

	// lvl6 corners (hori, vert)
	bvec2 lvl6x = bvec2(lvl5x.y && ((D1h.y && D1h.x)), lvl5w.y && ((B1v.w && B1v.x)));
	bvec2 lvl6y = bvec2(lvl5x.x && ((F1h.x && F1h.y)), lvl5y.y && ((B1v.z && B1v.y)));
	bvec2 lvl6z = bvec2(lvl5z.x && ((F1h.w && F1h.z)), lvl5y.x && ((H1v.y && H1v.z)));
	bvec2 lvl6w = bvec2(lvl5z.y && ((D1h.z && D1h.w)), lvl5w.x && ((H1v.x && H1v.w)));


	// subpixels - 0 = E, 1 = D, 2 = D0, 3 = F, 4 = F0, 5 = B, 6 = B0, 7 = H, 8 = H0

	vec4 crn;
	crn.x = ((lvl1x && Eo.x) || (lvl3x.x && Eo.y) || (lvl4x.x && Do.x) || (lvl6x.x && Fo.y)) ? 5.f : (lvl1x || (lvl3x.y && !Eo.w) || (lvl4x.y && !Bo.x) || (lvl6x.y && !Ho.w)) ? 1.f : lvl3x.x ? 3.f : lvl3x.y ? 7.f : lvl4x.x ? 2.f : lvl4x.y ? 6.f : lvl6x.x ? 4.f : lvl6x.y ? 8.f : 0.f;
	crn.y = ((lvl1y && Eo.y) || (lvl3y.x && Eo.x) || (lvl4y.x && Fo.y) || (lvl6y.x && Do.x)) ? 5.f : (lvl1y || (lvl3y.y && !Eo.z) || (lvl4y.y && !Bo.y) || (lvl6y.y && !Ho.z)) ? 3.f : lvl3y.x ? 1.f : lvl3y.y ? 7.f : lvl4y.x ? 4.f : lvl4y.y ? 6.f : lvl6y.x ? 2.f : lvl6y.y ? 8.f : 0.f;
	crn.z = ((lvl1z && Eo.z) || (lvl3z.x && Eo.w) || (lvl4z.x && Fo.z) || (lvl6z.x && Do.w)) ? 7.f : (lvl1z || (lvl3z.y && !Eo.y) || (lvl4z.y && !Ho.z) || (lvl6z.y && !Bo.y)) ? 3.f : lvl3z.x ? 1.f : lvl3z.y ? 5.f : lvl4z.x ? 4.f : lvl4z.y ? 8.f : lvl6z.x ? 2.f : lvl6z.y ? 6.f : 0.f;
	crn.w = ((lvl1w && Eo.w) || (lvl3w.x && Eo.z) || (lvl4w.x && Do.w) || (lvl6w.x && Fo.z)) ? 7.f : (lvl1w || (lvl3w.y && !Eo.x) || (lvl4w.y && !Ho.w) || (lvl6w.y && !Bo.x)) ? 1.f : lvl3w.x ? 3.f : lvl3w.y ? 5.f : lvl4w.x ? 2.f : lvl4w.y ? 8.f : lvl6w.x ? 4.f : lvl6w.y ? 6.f : 0.f;

	vec4 mid;
	mid.x = ((lvl2x.x &&  Eo.x) || (lvl2x.y &&  Eo.y) || (lvl5x.x &&  Do.x) || (lvl5x.y &&  Fo.y)) ? 5.f : lvl2x.x ? 1.f : lvl2x.y ? 3.f : lvl5x.x ? 2.f : lvl5x.y ? 4.f : ((Ec.x && Dc.z) && (Ec.y && Fc.w)) ? ( Eo.x ?  Eo.y ? 5.f : 3.f : 1.f) : 0.f;
	mid.y = ((lvl2y.x && !Eo.y) || (lvl2y.y && !Eo.z) || (lvl5y.x && !Bo.y) || (lvl5y.y && !Ho.z)) ? 3.f : lvl2y.x ? 5.f : lvl2y.y ? 7.f : lvl5y.x ? 6.f : lvl5y.y ? 8.f : ((Ec.y && Bc.w) && (Ec.z && Hc.x)) ? (!Eo.y ? !Eo.z ? 3.f : 7.f : 5.f) : 0.f;
	mid.z = ((lvl2z.x &&  Eo.w) || (lvl2z.y &&  Eo.z) || (lvl5z.x &&  Do.w) || (lvl5z.y &&  Fo.z)) ? 7.f : lvl2z.x ? 1.f : lvl2z.y ? 3.f : lvl5z.x ? 2.f : lvl5z.y ? 4.f : ((Ec.z && Fc.x) && (Ec.w && Dc.y)) ? ( Eo.z ?  Eo.w ? 7.f : 1.f : 3.f) : 0.f;
	mid.w = ((lvl2w.x && !Eo.x) || (lvl2w.y && !Eo.w) || (lvl5w.x && !Bo.x) || (lvl5w.y && !Ho.w)) ? 1.f : lvl2w.x ? 5.f : lvl2w.y ? 7.f : lvl5w.x ? 6.f : lvl5w.y ? 8.f : ((Ec.w && Hc.y) && (Ec.x && Bc.z)) ? (!Eo.w ? !Eo.x ? 1.f : 5.f : 7.f) : 0.f;


	// ouput
	tags[y*w+x] = (crn + 9.f * mid) / 80.f;
 }
 constexpr int dx[]{0,-1,-2,1,2,0,0,0,0},dy[]{0,0,0,0,0,-1,-2,1,2};
 for(int y=0;y<h;++y)for(int x=0;x<w;++x){
  const auto t=tags[y*w+x];
  const auto crn=floor(mod(t*80.f+.5f,9.f)),mid=floor(mod(t*8.888888f+.055555f,9.f));
  const int codes[]{int(crn.x),int(mid.x),int(crn.y),int(mid.w),0,int(mid.y),int(crn.w),int(mid.z),int(crn.z)};
  for(int j=0;j<9;++j){const auto k=codes[j];output[(y*3+j/3)*w*3+x*3+j%3]=input[index(x+dx[k],y+dy[k],w,h)];}
 }
}
} // namespace Frontend::ScaleFX

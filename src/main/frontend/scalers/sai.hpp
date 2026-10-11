// Adapted from Snes9x filter/2xsai.cpp, 2xSaI by Derek Liauw Kie Fa (1999-2001).
// Distributed under the Snes9x License; see SNES9X-LICENSE.txt.
// Changes: clamped sampling, ARGB8888 premultiplied interpolation, no packed
// 16-bit stores. Rule/branch ordering is retained from the upstream filters.
#pragma once
#include <array>
#include <cstdint>
namespace Frontend::SaI {
inline int GetResult(uint32_t A,uint32_t B,uint32_t C,uint32_t D){
 int x=0,y=0;
 if(A==C)++x;else if(B==C)++y;
 if(A==D)++x;else if(B==D)++y;
 return (x<=1)-(y<=1);
}
inline int GetResult1(uint32_t A,uint32_t B,uint32_t C,uint32_t D,uint32_t){return GetResult(A,B,C,D);}
inline int GetResult2(uint32_t A,uint32_t B,uint32_t C,uint32_t D,uint32_t){return -GetResult(A,B,C,D);}
inline uint32_t INTERPOLATE(uint32_t a,uint32_t b){
 return ((a&0xfefefefeu)>>1)+((b&0xfefefefeu)>>1)+(a&b&0x01010101u);
}
inline uint32_t Q_INTERPOLATE(uint32_t a,uint32_t b,uint32_t c,uint32_t d){
 const auto high=((a&0xfcfcfcfcu)>>2)+((b&0xfcfcfcfcu)>>2)+((c&0xfcfcfcfcu)>>2)+((d&0xfcfcfcfcu)>>2);
 const auto low=((a&0x03030303u)+(b&0x03030303u)+(c&0x03030303u)+(d&0x03030303u))>>2;
 return high+(low&0x03030303u);
}
template<class Sample> inline std::array<uint32_t,4> sai(Sample get){
uint32_t	colorA, colorB, colorC, colorD, colorE, colorF, colorG, colorH, colorI, colorJ, colorK, colorL, colorM, colorN, colorO, colorP;
            uint32_t	product, product1, product2;

            colorI = get(-1,-1);
            colorE = get(0,-1);
            colorF = get(1,-1);
            colorJ = get(2,-1);

            colorG = get(-1,0);
            colorA = get(0,0);
            colorB = get(1,0);
            colorK = get(2,0);

            colorH = get(-1,1);
            colorC = get(0,1);
            colorD = get(1,1);
            colorL = get(2,1);

            colorM = get(-1,2);
            colorN = get(0,2);
            colorO = get(1,2);
            colorP = get(2,2);

            if ((colorA == colorD) && (colorB != colorC))
            {
                if (((colorA == colorE) && (colorB == colorL)) || ((colorA == colorC) && (colorA == colorF) && (colorB != colorE) && (colorB == colorJ)))
                    product = colorA;
                else
                    product = INTERPOLATE(colorA, colorB);

                if (((colorA == colorG) && (colorC == colorO)) || ((colorA == colorB) && (colorA == colorH) && (colorG != colorC) && (colorC == colorM)))
                    product1 = colorA;
                else
                    product1 = INTERPOLATE(colorA, colorC);

                product2 = colorA;
            }
            else
            if ((colorB == colorC) && (colorA != colorD))
            {
                if (((colorB == colorF) && (colorA == colorH)) || ((colorB == colorE) && (colorB == colorD) && (colorA != colorF) && (colorA == colorI)))
                    product = colorB;
                else
                    product = INTERPOLATE(colorA, colorB);

                if (((colorC == colorH) && (colorA == colorF)) || ((colorC == colorG) && (colorC == colorD) && (colorA != colorH) && (colorA == colorI)))
                    product1 = colorC;
                else
                    product1 = INTERPOLATE(colorA, colorC);

                product2 = colorB;
            }
            else
            if ((colorA == colorD) && (colorB == colorC))
            {
                if (colorA == colorB)
                {
                    product  = colorA;
                    product1 = colorA;
                    product2 = colorA;
                }
                else
                {
                    int	r = 0;

                    product1 = INTERPOLATE(colorA, colorC);
                    product  = INTERPOLATE(colorA, colorB);

                    r += GetResult1(colorA, colorB, colorG, colorE, colorI);
                    r += GetResult2(colorB, colorA, colorK, colorF, colorJ);
                    r += GetResult2(colorB, colorA, colorH, colorN, colorM);
                    r += GetResult1(colorA, colorB, colorL, colorO, colorP);

                    if (r > 0)
                        product2 = colorA;
                    else
                    if (r < 0)
                        product2 = colorB;
                    else
                        product2 = Q_INTERPOLATE(colorA, colorB, colorC, colorD);
                }
            }
            else
            {
                product2 = Q_INTERPOLATE(colorA, colorB, colorC, colorD);

                if ((colorA == colorC) && (colorA == colorF) && (colorB != colorE) && (colorB == colorJ))
                    product  = colorA;
                else
                if ((colorB == colorE) && (colorB == colorD) && (colorA != colorF) && (colorA == colorI))
                    product  = colorB;
                else
                    product = INTERPOLATE(colorA, colorB);

                if ((colorA == colorB) && (colorA == colorH) && (colorG != colorC) && (colorC == colorM))
                    product1 = colorA;
                else
                if ((colorC == colorG) && (colorC == colorD) && (colorA != colorH) && (colorA == colorI))
                    product1 = colorC;
                else
                    product1 = INTERPOLATE(colorA, colorC);
            }

        return {colorA,product,product1,product2};
}
template<class Sample> inline std::array<uint32_t,4> super_sai(Sample get){
uint32_t	color1, color2, color3, color4, color5, color6;
            uint32_t	colorA0, colorA1, colorA2, colorA3, colorB0, colorB1, colorB2, colorB3, colorS1, colorS2;
            uint32_t	product1a, product1b, product2a, product2b;

            colorB0 = get(-1,-1);
            colorB1 = get(0,-1);
            colorB2 = get(1,-1);
            colorB3 = get(2,-1);

            color4  = get(-1,0);
            color5  = get(0,0);
            color6  = get(1,0);
            colorS2 = get(2,0);

            color1  = get(-1,1);
            color2  = get(0,1);
            color3  = get(1,1);
            colorS1 = get(2,1);

            colorA0 = get(-1,2);
            colorA1 = get(0,2);
            colorA2 = get(1,2);
            colorA3 = get(2,2);

            if (color2 == color6 && color5 != color3)
                product2b = product1b = color2;
            else
            if (color5 == color3 && color2 != color6)
                product2b = product1b = color5;
            else
            if (color5 == color3 && color2 == color6 && color5 != color6)
            {
                int	r = 0;

                r += GetResult(color6, color5, color1,  colorA1);
                r += GetResult(color6, color5, color4,  colorB1);
                r += GetResult(color6, color5, colorA2, colorS1);
                r += GetResult(color6, color5, colorB2, colorS2);

                if (r > 0)
                    product2b = product1b = color6;
                else
                if (r < 0)
                    product2b = product1b = color5;
                else
                    product2b = product1b = INTERPOLATE(color5, color6);
            }
            else
            {
                if (color6 == color3 && color3 == colorA1 && color2 != colorA2 && color3 != colorA0)
                    product2b = Q_INTERPOLATE(color3, color3, color3, color2);
                else
                if (color5 == color2 && color2 == colorA2 && colorA1 != color3 && color2 != colorA3)
                    product2b = Q_INTERPOLATE(color2, color2, color2, color3);
                else
                    product2b = INTERPOLATE(color2, color3);

                if (color6 == color3 && color6 == colorB1 && color5 != colorB2 && color6 != colorB0)
                    product1b = Q_INTERPOLATE(color6, color6, color6, color5);
                else
                if (color5 == color2 && color5 == colorB2 && colorB1 != color6 && color5 != colorB3)
                    product1b = Q_INTERPOLATE(color6, color5, color5, color5);
                else
                    product1b = INTERPOLATE (color5, color6);
            }

            if (color5 == color3 && color2 != color6 && color4 == color5 && color5 != colorA2)
                product2a = INTERPOLATE(color2, color5);
            else
            if (color5 == color1 && color6 == color5 && color4 != color2 && color5 != colorA0)
                product2a = INTERPOLATE(color2, color5);
            else
                product2a = color2;

            if (color2 == color6 && color5 != color3 && color1 == color2 && color2 != colorB2)
                product1a = INTERPOLATE(color2, color5);
            else
            if (color4 == color2 && color3 == color2 && color1 != color5 && color2 != colorB0)
                product1a = INTERPOLATE(color2, color5);
            else
                product1a = color5;

        return {product1a,product1b,product2a,product2b};
}
}

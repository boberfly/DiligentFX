"#include \"ToneMappingStructures.fxh\"\n"
"\n"
"#ifndef RGB_TO_LUMINANCE\n"
"#   define RGB_TO_LUMINANCE float3(0.212671, 0.715160, 0.072169)\n"
"#endif\n"
"\n"
"float3 Uncharted2Tonemap(float3 x)\n"
"{\n"
"    // http://www.gdcvault.com/play/1012459/Uncharted_2__HDR_Lighting\n"
"    // http://filmicgames.com/archives/75 - the coefficients are from here\n"
"    float A = 0.15; // Shoulder Strength\n"
"    float B = 0.50; // Linear Strength\n"
"    float C = 0.10; // Linear Angle\n"
"    float D = 0.20; // Toe Strength\n"
"    float E = 0.02; // Toe Numerator\n"
"    float F = 0.30; // Toe Denominator\n"
"    return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F))-E/F; // E/F = Toe Angle\n"
"}\n"
"\n"
"float3 ToneMap(in float3 f3Color, ToneMappingAttribs Attribs, float fAveLogLum)\n"
"{\n"
"    //const float middleGray = 1.03 - 2 / (2 + log10(fAveLogLum+1));\n"
"    float middleGray = Attribs.fMiddleGray;\n"
"    // Compute scale factor such that average luminance maps to middle gray\n"
"    float fLumScale = middleGray / fAveLogLum;\n"
"\n"
"    f3Color = max(f3Color, float3(0.0, 0.0, 0.0));\n"
"    float fInitialPixelLum = max(dot(RGB_TO_LUMINANCE, f3Color), 1e-10);\n"
"    float fScaledPixelLum = fInitialPixelLum * fLumScale;\n"
"    float3 f3ScaledColor = f3Color * fLumScale;\n"
"\n"
"    float whitePoint = Attribs.fWhitePoint;\n"
"\n"
"#   if TONE_MAPPING_MODE == TONE_MAPPING_MODE_EXP\n"
"    {\n"
"        float  fToneMappedLum = 1.0 - exp( -fScaledPixelLum );\n"
"        return fToneMappedLum * pow(f3Color / fInitialPixelLum, Attribs.fLuminanceSaturation * float3(1.0, 1.0, 1.0));\n"
"    }\n"
"#   elif TONE_MAPPING_MODE == TONE_MAPPING_MODE_REINHARD || TONE_MAPPING_MODE == TONE_MAPPING_MODE_REINHARD_MOD\n"
"    {\n"
"        // http://www.cs.utah.edu/~reinhard/cdrom/tonemap.pdf\n"
"        // http://imdoingitwrong.wordpress.com/2010/08/19/why-reinhard-desaturates-my-blacks-3/\n"
"        // http://content.gpwiki.org/index.php/D3DBook:High-Dynamic_Range_Rendering\n"
"\n"
"        float L_xy = fScaledPixelLum;\n"
"        float fToneMappedLum;\n"
"#       if TONE_MAPPING_MODE == TONE_MAPPING_MODE_REINHARD\n"
"        {\n"
"            fToneMappedLum = L_xy / (1.0 + L_xy);\n"
"        }\n"
"#       else\n"
"        {\n"
"	        fToneMappedLum = L_xy * (1.0 + L_xy / (whitePoint*whitePoint)) / (1.0 + L_xy);\n"
"        }\n"
"#       endif\n"
"	    return fToneMappedLum * pow(f3Color / fInitialPixelLum, Attribs.fLuminanceSaturation * float3(1.0, 1.0, 1.0));\n"
"    }\n"
"#elif TONE_MAPPING_MODE == TONE_MAPPING_MODE_UNCHARTED2\n"
"    {\n"
"        // http://filmicgames.com/archives/75\n"
"        float ExposureBias = 2.0;\n"
"        float3 curr = Uncharted2Tonemap(ExposureBias*f3ScaledColor);\n"
"        float3 whiteScale = float3(1.0, 1.0, 1.0) / Uncharted2Tonemap(float3(whitePoint, whitePoint, whitePoint));\n"
"        return curr*whiteScale;\n"
"    }\n"
"#elif TONE_MAPPING_MODE == TONE_MAPPING_FILMIC_ALU\n"
"    {\n"
"        // http://www.gdcvault.com/play/1012459/Uncharted_2__HDR_Lighting\n"
"        float3 f3ToneMappedColor = max(f3ScaledColor - float3(0.004, 0.004, 0.004), float3(0.0, 0.0, 0.0));\n"
"        f3ToneMappedColor = (f3ToneMappedColor * (6.2 * f3ToneMappedColor + float3(0.5, 0.5, 0.5))) /\n"
"                            (f3ToneMappedColor * (6.2 * f3ToneMappedColor + float3(1.7, 1.7, 1.7))+ float3(0.06, 0.06, 0.06));\n"
"        // result has 1/2.2 gamma baked in\n"
"        return pow(f3ToneMappedColor, float3(2.2, 2.2, 2.2));\n"
"    }\n"
"#elif TONE_MAPPING_MODE == TONE_MAPPING_LOGARITHMIC\n"
"    {\n"
"        // http://www.mpi-inf.mpg.de/resources/tmo/logmap/logmap.pdf\n"
"        float fToneMappedLum = log10(1.0 + fScaledPixelLum) / log10(1.0 + whitePoint);\n"
"	    return fToneMappedLum * pow(f3Color / fInitialPixelLum, Attribs.fLuminanceSaturation * float3(1.0, 1.0, 1.0));\n"
"    }\n"
"#elif TONE_MAPPING_MODE == TONE_MAPPING_ADAPTIVE_LOG\n"
"    {\n"
"        // http://www.mpi-inf.mpg.de/resources/tmo/logmap/logmap.pdf\n"
"        float Bias = 0.85;\n"
"        float fToneMappedLum =\n"
"            1.0 / log10(1.0 + whitePoint) *\n"
"            log(1.0 + fScaledPixelLum) / log( 2.0 + 8.0 * pow( fScaledPixelLum / whitePoint, log(Bias) / log(0.5)) );\n"
"	    return fToneMappedLum * pow(f3Color / fInitialPixelLum, Attribs.fLuminanceSaturation * float3(1.0, 1.0, 1.0));\n"
"    }\n"
"#else\n"
"    {\n"
"        return f3Color;\n"
"    }\n"
"#endif\n"
"}\n"

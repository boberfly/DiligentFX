"#ifndef _SHADER_UTILITIES_FXH_\n"
"#define _SHADER_UTILITIES_FXH_\n"
"\n"
"// Transforms depth to normalized device z coordinate\n"
"float DepthToNormalizedDeviceZ(in float Depth, in float4x4 mProj)\n"
"{\n"
"    // In Direct3D anv Vulkan, normalized device z range is [0, +1]\n"
"    // In OpengGL, normalized device z range is [-1, +1] (unless GL_ARB_clip_control extension is used to correct this nonsense).\n"
"    return MATRIX_ELEMENT(mProj,2,2) + MATRIX_ELEMENT(mProj,3,2) / Depth;\n"
"}\n"
"\n"
"#endif //_SHADER_UTILITIES_FXH_\n"
"#ifndef _RENDER_PBR_COMMON_FXH_\n"
"#define _RENDER_PBR_COMMON_FXH_\n"
"\n"
"struct PbrVsOutput\n"
"{\n"
"    float4 ClipPos  : SV_Position;\n"
"    float3 WorldPos : WORLD_POS;\n"
"    float3 Normal   : NORMAL;\n"
"    float2 UV0      : UV0;\n"
"    float2 UV1      : UV1;\n"
"};\n"
"\n"
"\n"
"#endif // _RENDER_PBR_COMMON_FXH_\n"
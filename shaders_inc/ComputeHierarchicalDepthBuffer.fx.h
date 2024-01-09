"#include \"SSR_Common.fxh\"\n"
"#include \"FullScreenTriangleVSOutput.fxh\"\n"
"\n"
"#if SUPPORTED_SHADER_SRV\n"
"Texture2D<float> g_TextureLastMip;\n"
"#else\n"
"cbuffer cbTextureMipAtrrib { int g_TextureMipIdx; }\n"
"Texture2D<float> g_TextureMips;\n"
"SamplerState     g_TextureMipsSampler;\n"
"#endif\n"
"\n"
"#if SUPPORTED_SHADER_SRV\n"
"float SampleDepth(uint2 Location, uint2 Offset, uint2 Dimension)\n"
"{\n"
"    uint2 Position = Location + Offset;\n"
"    if (Position.x >= Dimension.x || Position.y >= Dimension.y)\n"
"        return g_TextureLastMip.Load(int3(Location, 0));\n"
"    return g_TextureLastMip.Load(int3(Position, 0));\n"
"}\n"
"#else\n"
"float SampleDepth(uint2 Location, uint2 Offset, uint2 Dimension)\n"
"{\n"
"    uint2 Position = Location + Offset;\n"
"    if (Position.x >= Dimension.x || Position.y >= Dimension.y)\n"
"        return g_TextureMips.Load(int3(Location, g_TextureMipIdx));\n"
"    return g_TextureMips.Load(int3(Position, g_TextureMipIdx));\n"
"}\n"
"#endif\n"
"\n"
"float ComputeHierarchicalDepthBufferPS(in FullScreenTriangleVSOutput VSOut) : SV_Target0\n"
"{\n"
"    uint2 LastMipDimension;\n"
"#if SUPPORTED_SHADER_SRV\n"
"    g_TextureLastMip.GetDimensions(LastMipDimension.x, LastMipDimension.y);\n"
"#else\n"
"    uint Dummy;\n"
"    g_TextureMips.GetDimensions(0, LastMipDimension.x, LastMipDimension.y, Dummy);\n"
"    LastMipDimension.x = uint(floor(float(LastMipDimension.x) / exp2(float(g_TextureMipIdx))));\n"
"    LastMipDimension.y = uint(floor(float(LastMipDimension.y) / exp2(float(g_TextureMipIdx))));\n"
"#endif\n"
"\n"
"    uint2 RemappedPosition = uint2(2.0 * floor(VSOut.f4PixelPos.xy));\n"
"\n"
"    float4 SampledPixels;\n"
"    SampledPixels.x = SampleDepth(RemappedPosition, uint2(0, 0), LastMipDimension);\n"
"    SampledPixels.y = SampleDepth(RemappedPosition, uint2(0, 1), LastMipDimension);\n"
"    SampledPixels.z = SampleDepth(RemappedPosition, uint2(1, 0), LastMipDimension);\n"
"    SampledPixels.w = SampleDepth(RemappedPosition, uint2(1, 1), LastMipDimension);\n"
"\n"
"    float MinDepth = MipConvFunc(MipConvFunc(SampledPixels.x, SampledPixels.y), MipConvFunc(SampledPixels.z, SampledPixels.w));\n"
"\n"
"    bool IsWidthOdd  = (LastMipDimension.x & 1u) != 0u;\n"
"    bool IsHeightOdd = (LastMipDimension.y & 1u) != 0u;\n"
"\n"
"    if (IsWidthOdd)\n"
"    {\n"
"        SampledPixels.x = SampleDepth(RemappedPosition, uint2(2, 0), LastMipDimension);\n"
"        SampledPixels.y = SampleDepth(RemappedPosition, uint2(2, 1), LastMipDimension);\n"
"        MinDepth = MipConvFunc(MinDepth, MipConvFunc(SampledPixels.x, SampledPixels.y));\n"
"    }\n"
"\n"
"    if (IsHeightOdd)\n"
"    {\n"
"        SampledPixels.x = SampleDepth(RemappedPosition, uint2(0, 2), LastMipDimension);\n"
"        SampledPixels.y = SampleDepth(RemappedPosition, uint2(1, 2), LastMipDimension);\n"
"        MinDepth = MipConvFunc(MinDepth, MipConvFunc(SampledPixels.x, SampledPixels.y));\n"
"    }\n"
"\n"
"    if (IsWidthOdd && IsHeightOdd)\n"
"    {\n"
"        MinDepth = MipConvFunc(MinDepth, SampleDepth(RemappedPosition, uint2(2, 2), LastMipDimension));\n"
"    }\n"
"\n"
"    return MinDepth;\n"
"}\n"

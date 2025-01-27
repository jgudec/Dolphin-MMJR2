// Copyright 2009 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoBackends/Software/Rasterizer.h"

#include <algorithm>
#include <cstring>

#include "Common/CommonTypes.h"
#include "VideoBackends/Software/EfbInterface.h"
#include "VideoBackends/Software/NativeVertexFormat.h"
#include "VideoBackends/Software/Tev.h"
#include "VideoCommon/PerfQueryBase.h"
#include "VideoCommon/Statistics.h"
#include "VideoCommon/VideoCommon.h"
#include "VideoCommon/VideoConfig.h"
#include "VideoCommon/XFMemory.h"

namespace Rasterizer
{
static constexpr int BLOCK_SIZE = 2;

struct SlopeContext
{
  SlopeContext(const OutputVertexData* v0, const OutputVertexData* v1, const OutputVertexData* v2,
               s32 x0, s32 y0)
      : x0(x0), y0(y0)
  {
    // adjust a little less than 0.5
    const float adjust = 0.495f;

    xOff = ((float)x0 - v0->screenPosition.x) + adjust;
    yOff = ((float)y0 - v0->screenPosition.y) + adjust;

    dx10 = v1->screenPosition.x - v0->screenPosition.x;
    dx20 = v2->screenPosition.x - v0->screenPosition.x;
    dy10 = v1->screenPosition.y - v0->screenPosition.y;
    dy20 = v2->screenPosition.y - v0->screenPosition.y;
  }
  s32 x0;
  s32 y0;
  float xOff;
  float yOff;
  float dx10;
  float dx20;
  float dy10;
  float dy20;
};

struct Slope
{
  Slope() = default;
  Slope(float f0, float f1, float f2, const SlopeContext& ctx) : f0(f0)
  {
    float delta_20 = f2 - f0;
    float delta_10 = f1 - f0;

    //        x2 - x0    y1 - y0    x1 - x0    y2 - y0
    float a = delta_20 * ctx.dy10 - delta_10 * ctx.dy20;
    float b = ctx.dx20 * delta_10 - ctx.dx10 * delta_20;
    float c = ctx.dx20 * ctx.dy10 - ctx.dx10 * ctx.dy20;

    dfdx = a / c;
    dfdy = b / c;

    x0 = ctx.x0;
    y0 = ctx.y0;
    xOff = ctx.xOff;
    yOff = ctx.yOff;
  }

  // These default values are used in the unlikely case that zfreeze is enabled when drawing the
  // first primitive.
  // TODO: This is just a guess!
  float dfdx = 0.0f;
  float dfdy = 0.0f;
  float f0 = 1.0f;

  // Both an s32 value and a float value are used to minimize rounding error
  // TODO: is this really needed?
  s32 x0 = 0;
  s32 y0 = 0;
  float xOff = 0.0f;
  float yOff = 0.0f;

  float GetValue(s32 x, s32 y) const
  {
    float dx = xOff + (float)(x - x0);
    float dy = yOff + (float)(y - y0);
    return f0 + (dfdx * dx) + (dfdy * dy);
  }
};

static Slope ZSlope;
static Slope WSlope;
static Slope ColorSlopes[2][4];
static Slope TexSlopes[8][3];

static Tev tev;
static RasterBlock rasterBlock;

void Init()
{
  tev.Init();

  // The other slopes are set each for each primitive drawn, but zfreeze means that the z slope
  // needs to be set to an (untested) default value.
  ZSlope = Slope();
}

// Returns approximation of log2(f) in s28.4
// results are close enough to use for LOD
static s32 FixedLog2(float f)
{
  u32 x;
  std::memcpy(&x, &f, sizeof(u32));

  s32 logInt = ((x & 0x7F800000) >> 19) - 2032;  // integer part
  s32 logFract = (x & 0x007fffff) >> 19;         // approximate fractional part

  return logInt + logFract;
}

static inline int iround(float x)
{
  int t = (int)x;
  if ((x - t) >= 0.5)
    return t + 1;

  return t;
}

void SetTevReg(int reg, int comp, s16 color)
{
  tev.SetRegColor(reg, comp, color);
}

static void Draw(s32 x, s32 y, s32 xi, s32 yi)
{
  INCSTAT(g_stats.this_frame.rasterized_pixels);

  s32 z = (s32)std::clamp<float>(ZSlope.GetValue(x, y), 0.0f, 16777215.0f);

  if (bpmem.GetEmulatedZ() == EmulatedZ::Early)
  {
    // TODO: Test if perf regs are incremented even if test is disabled
    EfbInterface::IncPerfCounterQuadCount(PQ_ZCOMP_INPUT_ZCOMPLOC);
    if (bpmem.zmode.testenable)
    {
      // early z
      if (!EfbInterface::ZCompare(x, y, z))
        return;
    }
    EfbInterface::IncPerfCounterQuadCount(PQ_ZCOMP_OUTPUT_ZCOMPLOC);
  }

  RasterBlockPixel& pixel = rasterBlock.Pixel[xi][yi];

  tev.Position[0] = x;
  tev.Position[1] = y;
  tev.Position[2] = z;

  //  colors
  for (unsigned int i = 0; i < bpmem.genMode.numcolchans; i++)
  {
    for (int comp = 0; comp < 4; comp++)
    {
      u16 color = (u16)ColorSlopes[i][comp].GetValue(x, y);

      // clamp color value to 0
      u16 mask = ~(color >> 8);

      tev.Color[i][comp] = color & mask;
    }
  }

  // tex coords
  for (unsigned int i = 0; i < bpmem.genMode.numtexgens; i++)
  {
    // multiply by 128 because TEV stores UVs as s17.7
    tev.Uv[i].s = (s32)(pixel.Uv[i][0] * 128);
    tev.Uv[i].t = (s32)(pixel.Uv[i][1] * 128);
  }

  for (unsigned int i = 0; i < bpmem.genMode.numindstages; i++)
  {
    tev.IndirectLod[i] = rasterBlock.IndirectLod[i];
    tev.IndirectLinear[i] = rasterBlock.IndirectLinear[i];
  }

  for (unsigned int i = 0; i <= bpmem.genMode.numtevstages; i++)
  {
    tev.TextureLod[i] = rasterBlock.TextureLod[i];
    tev.TextureLinear[i] = rasterBlock.TextureLinear[i];
  }

  tev.Draw();
}

static inline void CalculateLOD(s32* lodp, bool* linear, u32 texmap, u32 texcoord)
{
  auto texUnit = bpmem.tex.GetUnit(texmap);

  // LOD calculation requires data from the texture mode for bias, etc.
  // it does not seem to use the actual texture size
  const TexMode0& tm0 = texUnit.texMode0;
  const TexMode1& tm1 = texUnit.texMode1;

  float sDelta, tDelta;

  float* uv00 = rasterBlock.Pixel[0][0].Uv[texcoord];
  float* uv10 = rasterBlock.Pixel[1][0].Uv[texcoord];
  float* uv01 = rasterBlock.Pixel[0][1].Uv[texcoord];

  float dudx = fabsf(uv00[0] - uv10[0]);
  float dvdx = fabsf(uv00[1] - uv10[1]);
  float dudy = fabsf(uv00[0] - uv01[0]);
  float dvdy = fabsf(uv00[1] - uv01[1]);

  if (tm0.diag_lod == LODType::Diagonal)
  {
    sDelta = dudx + dudy;
    tDelta = dvdx + dvdy;
  }
  else
  {
    sDelta = std::max(dudx, dudy);
    tDelta = std::max(dvdx, dvdy);
  }

  // get LOD in s28.4
  s32 lod = FixedLog2(std::max(sDelta, tDelta));

  // bias is s2.5
  int bias = tm0.lod_bias;
  bias >>= 1;
  lod += bias;

  *linear = ((lod > 0 && tm0.min_filter == FilterMode::Linear) ||
             (lod <= 0 && tm0.mag_filter == FilterMode::Linear));

  // NOTE: The order of comparisons for this clamp check matters.
  if (lod > static_cast<s32>(tm1.max_lod))
    lod = static_cast<s32>(tm1.max_lod);
  else if (lod < static_cast<s32>(tm1.min_lod))
    lod = static_cast<s32>(tm1.min_lod);

  *lodp = lod;
}

static void BuildBlock(s32 blockX, s32 blockY)
{
  for (s32 yi = 0; yi < BLOCK_SIZE; yi++)
  {
    for (s32 xi = 0; xi < BLOCK_SIZE; xi++)
    {
      RasterBlockPixel& pixel = rasterBlock.Pixel[xi][yi];

      s32 x = xi + blockX;
      s32 y = yi + blockY;

      float invW = 1.0f / WSlope.GetValue(x, y);
      pixel.InvW = invW;

      // tex coords
      for (unsigned int i = 0; i < bpmem.genMode.numtexgens; i++)
      {
        float projection = invW;
        float q = TexSlopes[i][2].GetValue(x, y) * invW;
        if (q != 0.0f)
          projection = invW / q;

        pixel.Uv[i][0] = TexSlopes[i][0].GetValue(x, y) * projection;
        pixel.Uv[i][1] = TexSlopes[i][1].GetValue(x, y) * projection;
      }
    }
  }

  u32 indref = bpmem.tevindref.hex;
  for (unsigned int i = 0; i < bpmem.genMode.numindstages; i++)
  {
    u32 texmap = indref & 7;
    indref >>= 3;
    u32 texcoord = indref & 7;
    indref >>= 3;

    CalculateLOD(&rasterBlock.IndirectLod[i], &rasterBlock.IndirectLinear[i], texmap, texcoord);
  }

  for (unsigned int i = 0; i <= bpmem.genMode.numtevstages; i++)
  {
    int stageOdd = i & 1;
    const TwoTevStageOrders& order = bpmem.tevorders[i >> 1];
    if (order.getEnable(stageOdd))
    {
      u32 texmap = order.getTexMap(stageOdd);
      u32 texcoord = order.getTexCoord(stageOdd);

      CalculateLOD(&rasterBlock.TextureLod[i], &rasterBlock.TextureLinear[i], texmap, texcoord);
    }
  }
}

void UpdateZSlope(const OutputVertexData* v0, const OutputVertexData* v1,
                  const OutputVertexData* v2)
{
  if (!bpmem.genMode.zfreeze)
  {
    const s32 X1 = iround(16.0f * v0->screenPosition[0]) - 9;
    const s32 Y1 = iround(16.0f * v0->screenPosition[1]) - 9;
    const SlopeContext ctx(v0, v1, v2, (X1 + 0xF) >> 4, (Y1 + 0xF) >> 4);
    ZSlope = Slope(v0->screenPosition.z, v1->screenPosition.z, v2->screenPosition.z, ctx);
  }
}

void DrawTriangleFrontFace(const OutputVertexData* v0, const OutputVertexData* v1,
                           const OutputVertexData* v2)
{
  INCSTAT(g_stats.this_frame.num_triangles_drawn);

  // The zslope should be updated now, even if the triangle is rejected by the scissor test, as
  // zfreeze depends on it
  UpdateZSlope(v0, v1, v2);

  // adapted from http://devmaster.net/posts/6145/advanced-rasterization

  // 28.4 fixed-pou32 coordinates. rounded to nearest and adjusted to match hardware output
  // could also take floor and adjust -8
  const s32 Y1 = iround(16.0f * v0->screenPosition[1]) - 9;
  const s32 Y2 = iround(16.0f * v1->screenPosition[1]) - 9;
  const s32 Y3 = iround(16.0f * v2->screenPosition[1]) - 9;

  const s32 X1 = iround(16.0f * v0->screenPosition[0]) - 9;
  const s32 X2 = iround(16.0f * v1->screenPosition[0]) - 9;
  const s32 X3 = iround(16.0f * v2->screenPosition[0]) - 9;

  // Deltas
  const s32 DX12 = X1 - X2;
  const s32 DX23 = X2 - X3;
  const s32 DX31 = X3 - X1;

  const s32 DY12 = Y1 - Y2;
  const s32 DY23 = Y2 - Y3;
  const s32 DY31 = Y3 - Y1;

  // Fixed-pos32 deltas
  const s32 FDX12 = DX12 * 16;
  const s32 FDX23 = DX23 * 16;
  const s32 FDX31 = DX31 * 16;

  const s32 FDY12 = DY12 * 16;
  const s32 FDY23 = DY23 * 16;
  const s32 FDY31 = DY31 * 16;

  // Bounding rectangle
  s32 minx = (std::min(std::min(X1, X2), X3) + 0xF) >> 4;
  s32 maxx = (std::max(std::max(X1, X2), X3) + 0xF) >> 4;
  s32 miny = (std::min(std::min(Y1, Y2), Y3) + 0xF) >> 4;
  s32 maxy = (std::max(std::max(Y1, Y2), Y3) + 0xF) >> 4;

  // scissor
  s32 xoff = bpmem.scissorOffset.x * 2;
  s32 yoff = bpmem.scissorOffset.y * 2;

  s32 scissorLeft = bpmem.scissorTL.x - xoff;
  if (scissorLeft < 0)
    scissorLeft = 0;

  s32 scissorTop = bpmem.scissorTL.y - yoff;
  if (scissorTop < 0)
    scissorTop = 0;

  s32 scissorRight = bpmem.scissorBR.x - xoff + 1;
  if (scissorRight > s32(EFB_WIDTH))
    scissorRight = EFB_WIDTH;

  s32 scissorBottom = bpmem.scissorBR.y - yoff + 1;
  if (scissorBottom > s32(EFB_HEIGHT))
    scissorBottom = EFB_HEIGHT;

  minx = std::max(minx, scissorLeft);
  maxx = std::min(maxx, scissorRight);
  miny = std::max(miny, scissorTop);
  maxy = std::min(maxy, scissorBottom);

  if (minx >= maxx || miny >= maxy)
    return;

  // Set up the remaining slopes
  const SlopeContext ctx(v0, v1, v2, (X1 + 0xF) >> 4, (Y1 + 0xF) >> 4);

  float w[3] = {1.0f / v0->projectedPosition.w, 1.0f / v1->projectedPosition.w,
                1.0f / v2->projectedPosition.w};
  WSlope = Slope(w[0], w[1], w[2], ctx);

  for (unsigned int i = 0; i < bpmem.genMode.numcolchans; i++)
  {
    for (int comp = 0; comp < 4; comp++)
      ColorSlopes[i][comp] = Slope(v0->color[i][comp], v1->color[i][comp], v2->color[i][comp], ctx);
  }

  for (unsigned int i = 0; i < bpmem.genMode.numtexgens; i++)
  {
    for (int comp = 0; comp < 3; comp++)
    {
      TexSlopes[i][comp] = Slope(v0->texCoords[i][comp] * w[0], v1->texCoords[i][comp] * w[1],
                                 v2->texCoords[i][comp] * w[2], ctx);
    }
  }

  // Half-edge constants
  s32 C1 = DY12 * X1 - DX12 * Y1;
  s32 C2 = DY23 * X2 - DX23 * Y2;
  s32 C3 = DY31 * X3 - DX31 * Y3;

  // Correct for fill convention
  if (DY12 < 0 || (DY12 == 0 && DX12 > 0))
    C1++;
  if (DY23 < 0 || (DY23 == 0 && DX23 > 0))
    C2++;
  if (DY31 < 0 || (DY31 == 0 && DX31 > 0))
    C3++;

  // Start in corner of 8x8 block
  minx &= ~(BLOCK_SIZE - 1);
  miny &= ~(BLOCK_SIZE - 1);

  // Loop through blocks
  for (s32 y = miny; y < maxy; y += BLOCK_SIZE)
  {
    for (s32 x = minx; x < maxx; x += BLOCK_SIZE)
    {
      // Corners of block
      s32 x0 = x << 4;
      s32 x1 = (x + BLOCK_SIZE - 1) << 4;
      s32 y0 = y << 4;
      s32 y1 = (y + BLOCK_SIZE - 1) << 4;

      // Evaluate half-space functions
      bool a00 = C1 + DX12 * y0 - DY12 * x0 > 0;
      bool a10 = C1 + DX12 * y0 - DY12 * x1 > 0;
      bool a01 = C1 + DX12 * y1 - DY12 * x0 > 0;
      bool a11 = C1 + DX12 * y1 - DY12 * x1 > 0;
      int a = (a00 << 0) | (a10 << 1) | (a01 << 2) | (a11 << 3);

      bool b00 = C2 + DX23 * y0 - DY23 * x0 > 0;
      bool b10 = C2 + DX23 * y0 - DY23 * x1 > 0;
      bool b01 = C2 + DX23 * y1 - DY23 * x0 > 0;
      bool b11 = C2 + DX23 * y1 - DY23 * x1 > 0;
      int b = (b00 << 0) | (b10 << 1) | (b01 << 2) | (b11 << 3);

      bool c00 = C3 + DX31 * y0 - DY31 * x0 > 0;
      bool c10 = C3 + DX31 * y0 - DY31 * x1 > 0;
      bool c01 = C3 + DX31 * y1 - DY31 * x0 > 0;
      bool c11 = C3 + DX31 * y1 - DY31 * x1 > 0;
      int c = (c00 << 0) | (c10 << 1) | (c01 << 2) | (c11 << 3);

      // Skip block when outside an edge
      if (a == 0x0 || b == 0x0 || c == 0x0)
        continue;

      BuildBlock(x, y);

      // Accept whole block when totally covered
      if (a == 0xF && b == 0xF && c == 0xF)
      {
        for (s32 iy = 0; iy < BLOCK_SIZE; iy++)
        {
          for (s32 ix = 0; ix < BLOCK_SIZE; ix++)
          {
            Draw(x + ix, y + iy, ix, iy);
          }
        }
      }
      else  // Partially covered block
      {
        s32 CY1 = C1 + DX12 * y0 - DY12 * x0;
        s32 CY2 = C2 + DX23 * y0 - DY23 * x0;
        s32 CY3 = C3 + DX31 * y0 - DY31 * x0;

        for (s32 iy = 0; iy < BLOCK_SIZE; iy++)
        {
          s32 CX1 = CY1;
          s32 CX2 = CY2;
          s32 CX3 = CY3;

          for (s32 ix = 0; ix < BLOCK_SIZE; ix++)
          {
            if (CX1 > 0 && CX2 > 0 && CX3 > 0)
            {
              Draw(x + ix, y + iy, ix, iy);
            }

            CX1 -= FDY12;
            CX2 -= FDY23;
            CX3 -= FDY31;
          }

          CY1 += FDX12;
          CY2 += FDX23;
          CY3 += FDX31;
        }
      }
    }
  }
}
}  // namespace Rasterizer

// RSXGL - Graphics library for the PS3 GPU.
//
// Copyright (c) 2011 Alexander Betts (alex.betts@gmail.com)
//
// state.cc - GL functions mostly covered by section 4.1 of the OpenGL 3.1 spec.

#include <GL3/gl3.h>

#include "gcm.h"
#include "rsxgl_context.h"
#include "gl_fifo.h"
#include "nv40.h"
#include "rsxgl_assert.h"
#include "error.h"
#include "state.h"
#include "cxxutil.h"

#if defined(GLAPI)
#undef GLAPI
#endif
#define GLAPI extern "C"

state_t::state_t()
{
  invalid.all = ~0;

  viewport.x = 0;
  viewport.y = 0;
  viewport.width = 0;
  viewport.height = 0;
  viewport.depthRange[0] = 0.0f;
  viewport.depthRange[1] = 1.0f;
  viewport.cullNearFar = 1;
  viewport.clampZ = 0;
  viewport.cullIgnoreW = 0;

  enable.scissor = 0;
  scissor.x = 0;
  scissor.y = 0;
  scissor.width = 4096;
  scissor.height = 4096;

  write_mask.r = 1;
  write_mask.g = 1;
  write_mask.b = 1;
  write_mask.a = 1;
  color.clear = 0;

  write_mask.depth = 1;
  enable.depth_test = 0;
  depth.clear = 0xffff;
  depth.func = RSXGL_ALWAYS;

  enable.blend = 0;
  blend.color = 0;
  blend.rgb_equation = RSXGL_FUNC_ADD;
  blend.alpha_equation = RSXGL_FUNC_ADD;
  blend.src_rgb_func = RSXGL_ONE;
  blend.src_alpha_func = RSXGL_ONE;
  blend.dst_rgb_func = RSXGL_ZERO;
  blend.dst_alpha_func = RSXGL_ZERO;

  stencil.face[0].enable = 0;
  stencil.face[0].mask = 0xff;
  stencil.face[0].func = RSXGL_ALWAYS;
  stencil.face[0].fail_op = RSXGL_KEEP;
  stencil.face[0].zfail_op = RSXGL_KEEP;
  stencil.face[0].pass_op = RSXGL_KEEP;

  stencil.face[1].enable = 0;
  stencil.face[1].mask = 0xff;
  stencil.face[1].func = RSXGL_ALWAYS;
  stencil.face[1].fail_op = RSXGL_KEEP;
  stencil.face[1].zfail_op = RSXGL_KEEP;
  stencil.face[1].pass_op = RSXGL_KEEP;

  polygon.cullEnable = 0;
  polygon.cullFace = RSXGL_CULL_BACK;
  polygon.frontFace = RSXGL_FACE_CCW;
  polygon.mode = RSXGL_POLYGON_MODE_FILL;
  polygon.offsetFactor = 0;
  polygon.offsetUnits = 0;

  lineWidth = 1.0f;
  pointSize = 1.0f;

  pixel_store.pack_swap_bytes = 0;
  pixel_store.pack_lsb_first = 0;
  pixel_store.pack_alignment = RSXGL_PIXEL_STORE_ALIGNMENT_4;
  pixel_store.unpack_swap_bytes = 0;
  pixel_store.unpack_lsb_first = 0;
  pixel_store.unpack_alignment = RSXGL_PIXEL_STORE_ALIGNMENT_4;
  pixel_store.pack_row_length = 0;
  pixel_store.pack_image_height = 0;
  pixel_store.pack_skip_pixels = 0;
  pixel_store.pack_skip_rows = 0;
  pixel_store.pack_skip_images = 0;
  pixel_store.unpack_row_length = 0;
  pixel_store.unpack_image_height = 0;
  pixel_store.unpack_skip_pixels = 0;
  pixel_store.unpack_skip_rows = 0;

  enable.primitive_restart = 0;
  primitiveRestartIndex = 0;
}

union _ieee32_t {
  float f;
  uint32_t u;
  struct {
    uint16_t a[2];
  } h;
  struct {
    uint8_t a[4];
  } b;

  _ieee32_t(uint32_t value)
    : u(value) {
  }

  _ieee32_t(float value)
    : f(value) {
  }
};

static inline void
rsxgl_emit_color_write_mask(gcmContextData * context,uint32_t r,uint32_t g,uint32_t b,uint32_t a)
{
  uint32_t * buffer = 0;

  buffer = gcm_reserve(context,2);

  gcm_emit_method(&buffer,NV30_3D_COLOR_MASK,1);
  gcm_emit(&buffer,
	   (r ? NV30_3D_COLOR_MASK_R : 0) |
	   (g ? NV30_3D_COLOR_MASK_G : 0) |
	   (b ? NV30_3D_COLOR_MASK_B : 0) |
	   (a ? NV30_3D_COLOR_MASK_A : 0));
  
  gcm_finish_commands(context,&buffer);
}

static inline void
rsxgl_emit_depth_write_mask(gcmContextData * context,uint32_t depth)
{
  uint32_t * buffer = gcm_reserve(context,2);

  gcm_emit_method(&buffer,NV30_3D_DEPTH_WRITE_ENABLE,1);
  gcm_emit(&buffer,depth);

  gcm_finish_commands(context,&buffer);
}

static inline uint32_t
nv40_blend_func(uint32_t x)
{
  switch(x) {
  case RSXGL_ZERO:
    return NV30_3D_BLEND_FUNC_SRC_RGB_ZERO;
  case RSXGL_ONE:
    return NV30_3D_BLEND_FUNC_SRC_RGB_ONE;
  case RSXGL_SRC_COLOR:
    return NV30_3D_BLEND_FUNC_SRC_RGB_SRC_COLOR;
  case RSXGL_ONE_MINUS_SRC_COLOR:
    return NV30_3D_BLEND_FUNC_SRC_RGB_ONE_MINUS_SRC_COLOR;
  case RSXGL_SRC_ALPHA:
    return NV30_3D_BLEND_FUNC_SRC_RGB_SRC_ALPHA;
  case RSXGL_ONE_MINUS_SRC_ALPHA:
    return NV30_3D_BLEND_FUNC_SRC_RGB_ONE_MINUS_SRC_ALPHA;
  case RSXGL_DST_ALPHA:
    return NV30_3D_BLEND_FUNC_SRC_RGB_DST_ALPHA;
  case RSXGL_ONE_MINUS_DST_ALPHA:
    return NV30_3D_BLEND_FUNC_SRC_RGB_ONE_MINUS_DST_ALPHA;
  case RSXGL_DST_COLOR:
    return NV30_3D_BLEND_FUNC_SRC_RGB_DST_COLOR;
  case RSXGL_ONE_MINUS_DST_COLOR:
    return NV30_3D_BLEND_FUNC_SRC_RGB_ONE_MINUS_DST_COLOR;
  case RSXGL_SRC_ALPHA_SATURATE:
    return NV30_3D_BLEND_FUNC_SRC_RGB_SRC_ALPHA_SATURATE;
  case RSXGL_CONSTANT_COLOR:
    return NV30_3D_BLEND_FUNC_SRC_RGB_CONSTANT_COLOR;
  case RSXGL_ONE_MINUS_CONSTANT_COLOR:
    return NV30_3D_BLEND_FUNC_SRC_RGB_ONE_MINUS_CONSTANT_COLOR;
  case RSXGL_CONSTANT_ALPHA:
    return NV30_3D_BLEND_FUNC_SRC_RGB_CONSTANT_ALPHA;
  case RSXGL_ONE_MINUS_CONSTANT_ALPHA:
    return NV30_3D_BLEND_FUNC_SRC_RGB_ONE_MINUS_CONSTANT_ALPHA;
  default:
    rsxgl_assert(0);
  };
}

static inline uint32_t
nv40_blend_equation(uint32_t x)
{
  switch(x) {
  case RSXGL_FUNC_ADD:
    return NV40_3D_BLEND_EQUATION_RGB_FUNC_ADD;
  case RSXGL_FUNC_MIN:
    return NV30_3D_BLEND_EQUATION_MIN;
  case RSXGL_FUNC_MAX:
    return NV30_3D_BLEND_EQUATION_MAX;
  case RSXGL_FUNC_SUBTRACT:
    return NV30_3D_BLEND_EQUATION_FUNC_SUBTRACT;
  case RSXGL_FUNC_REVERSE_SUBTRACT:
    return NV30_3D_BLEND_EQUATION_FUNC_REVERSE_SUBTRACT;
  default:
    rsxgl_assert(0);
  };
};

static inline
void rsxgl_emit_scissor(gcmContextData * context,uint16_t x,uint16_t y,uint16_t w,uint16_t h)
{
  uint32_t * buffer = gcm_reserve(context,3);

  gcm_emit_method(&buffer,NV30_3D_SCISSOR_HORIZ,2);
  gcm_emit(&buffer,((uint32_t)w << 16) | ((uint32_t)x));
  gcm_emit(&buffer,((uint32_t)h << 16) | ((uint32_t)y));

  gcm_finish_commands(context,&buffer);  
}

void
rsxgl_state_validate(rsxgl_context_t * ctx)
{
  gcmContextData * context = ctx -> base.gcm_context;
  state_t * s = &ctx -> state;

  uint32_t * buffer = 0;
  
  // viewport & depth range:
  if(s -> invalid.parts.viewport || s -> invalid.parts.depth_range) {
    _ieee32_t scale[4] = {
      s -> viewport.width * 0.5f,
      s -> viewport.height * -0.5f,
      (s -> viewport.depthRange[1] - s -> viewport.depthRange[0]) * 0.5f,
      0.0f
    };
    _ieee32_t offset[4] = {
      s -> viewport.x + (s -> viewport.width * 0.5f),
      s -> viewport.y + (s -> viewport.height * 0.5f),
      (s -> viewport.depthRange[1] + s -> viewport.depthRange[0]) * 0.5f,
      0.0f
    };

    buffer = gcm_reserve(context,17);

    gcm_emit_method(&buffer,NV30_3D_VIEWPORT_HORIZ,2);
    gcm_emit(&buffer,((uint32_t)s -> viewport.width << 16) | ((uint32_t)s -> viewport.x));
    gcm_emit(&buffer,((uint32_t)s -> viewport.height << 16) | ((uint32_t)s -> viewport.y));

    gcm_emit_method(&buffer,NV30_3D_DEPTH_RANGE_NEAR,2);
    gcm_emit(&buffer,_ieee32_t(s -> viewport.depthRange[0]).u);
    gcm_emit(&buffer,_ieee32_t(s -> viewport.depthRange[1]).u);;

    gcm_emit_method(&buffer,NV30_3D_VIEWPORT_TRANSLATE,8);
    gcm_emit(&buffer,offset[0].u);
    gcm_emit(&buffer,offset[1].u);
    gcm_emit(&buffer,offset[2].u);
    gcm_emit(&buffer,offset[3].u);
    gcm_emit(&buffer,scale[0].u);
    gcm_emit(&buffer,scale[1].u);
    gcm_emit(&buffer,scale[2].u);
    gcm_emit(&buffer,scale[3].u);

    gcm_emit_method(&buffer,NV30_3D_DEPTH_CONTROL,1);
    gcm_emit(&buffer,((uint32_t)s -> viewport.cullNearFar) | ((uint32_t)s -> viewport.clampZ << 4) | ((uint32_t)s -> viewport.cullIgnoreW << 8));

    gcm_finish_commands(context,&buffer);

    s -> invalid.parts.viewport = 0;
    s -> invalid.parts.depth_range = 0;
  }

  // scissor:
  if(s -> invalid.parts.scissor) {
    if(s -> enable.scissor) {
      rsxgl_emit_scissor(context,s -> scissor.x,s -> scissor.y,s -> scissor.width,s -> scissor.height);
    }
    else {
      rsxgl_emit_scissor(context,0,0,4096,4096);
    }

    s -> invalid.parts.scissor = 0;
  }

  // color write mask:
  if(s -> invalid.parts.color_write_mask) {
    rsxgl_emit_color_write_mask(context,s -> write_mask.r,s -> write_mask.g,s -> write_mask.b,s -> write_mask.a);
    s -> invalid.parts.color_write_mask = 0;
  }

  // depth write mask:
  if(s -> invalid.parts.depth_write_mask) {
    rsxgl_emit_depth_write_mask(context,s -> write_mask.depth);
    s -> invalid.parts.depth_write_mask = 0;
  }

  // TODO - Finer-grained lazy evaluation of all of the following state:
  //
  if(s -> invalid.parts.the_rest == 0) return;

  // clear color:
  buffer = gcm_reserve(context,2);
  
  gcm_emit_method(&buffer,NV30_3D_CLEAR_COLOR_VALUE,1);
  gcm_emit(&buffer,s -> color.clear);

  gcm_finish_commands(context,&buffer);

  // depth-related:
  buffer = gcm_reserve(context,6);

  gcm_emit_method(&buffer,NV30_3D_DEPTH_TEST_ENABLE,1);
  gcm_emit(&buffer,s -> enable.depth_test);

  gcm_emit_method(&buffer,NV30_3D_DEPTH_FUNC,1);
  switch(s -> depth.func) {
  case RSXGL_NEVER:
    gcm_emit(&buffer,NV30_3D_DEPTH_FUNC_NEVER);
    break;
  case RSXGL_LESS:
    gcm_emit(&buffer,NV30_3D_DEPTH_FUNC_LESS);
    break;
  case RSXGL_EQUAL:
    gcm_emit(&buffer,NV30_3D_DEPTH_FUNC_EQUAL);
    break;
  case RSXGL_LEQUAL:
    gcm_emit(&buffer,NV30_3D_DEPTH_FUNC_LEQUAL);
    break;
  case RSXGL_GREATER:
    gcm_emit(&buffer,NV30_3D_DEPTH_FUNC_GREATER);
    break;
  case RSXGL_NOTEQUAL:
    gcm_emit(&buffer,NV30_3D_DEPTH_FUNC_NOTEQUAL);
    break;
  case RSXGL_GEQUAL:
    gcm_emit(&buffer,NV30_3D_DEPTH_FUNC_GEQUAL);
    break;
  case RSXGL_ALWAYS:
    gcm_emit(&buffer,NV30_3D_DEPTH_FUNC_ALWAYS);
    break;
  };

  gcm_emit_method(&buffer,NV30_3D_CLEAR_DEPTH_VALUE,1);
  gcm_emit(&buffer,s -> depth.clear);

  gcm_finish_commands(context,&buffer);

  // blending:
  if(s -> enable.blend) {
    buffer = gcm_reserve(context,9);

    gcm_emit_method(&buffer,NV30_3D_BLEND_FUNC_ENABLE,1);
    gcm_emit(&buffer,1);

    gcm_emit_method(&buffer,NV30_3D_BLEND_COLOR,1);
    gcm_emit(&buffer,s -> blend.color);

    gcm_emit_method(&buffer,NV30_3D_BLEND_FUNC_SRC,2);

    gcm_emit(&buffer,nv40_blend_func(s -> blend.src_rgb_func) | nv40_blend_func(s -> blend.src_alpha_func) << NV30_3D_BLEND_FUNC_SRC_ALPHA__SHIFT);
    gcm_emit(&buffer,nv40_blend_func(s -> blend.dst_rgb_func) | nv40_blend_func(s -> blend.dst_alpha_func) << NV30_3D_BLEND_FUNC_SRC_ALPHA__SHIFT);

    gcm_emit_method(&buffer,NV40_3D_BLEND_EQUATION,1);

    gcm_emit(&buffer,nv40_blend_equation(s -> blend.rgb_equation) | nv40_blend_equation(s -> blend.alpha_equation) << NV40_3D_BLEND_EQUATION_ALPHA__SHIFT);
  }
  else {
    buffer = gcm_reserve(context,2);

    gcm_emit_method(&buffer,NV30_3D_BLEND_FUNC_ENABLE,1);
    gcm_emit(&buffer,0);
  }

  gcm_finish_commands(context,&buffer);

  // stencil:
  for(int f = 0;f < 2;++f) {
    if(s -> stencil.face[f].enable) {
      buffer = gcm_reserve(context,9);

      gcm_emit_method(&buffer,NV30_3D_STENCIL_ENABLE(f),8);
      gcm_emit(&buffer,1);
      gcm_emit(&buffer,s -> stencil.face[f].writemask);
      gcm_emit(&buffer,s -> stencil.face[f].func);
      gcm_emit(&buffer,s -> stencil.face[f].ref);
      gcm_emit(&buffer,s -> stencil.face[f].mask);
      gcm_emit(&buffer,s -> stencil.face[f].fail_op);
      gcm_emit(&buffer,s -> stencil.face[f].zfail_op);
      gcm_emit(&buffer,s -> stencil.face[f].pass_op);
    }
    else {
      buffer = gcm_reserve(context,2);

      gcm_emit_method(&buffer,NV30_3D_STENCIL_ENABLE(f),1);
      gcm_emit(&buffer,0);
    }

    gcm_finish_commands(context,&buffer);
  }

  // polygon culling:
  if(s -> polygon.cullEnable) {
    buffer = gcm_reserve(context,4);

    gcm_emit_method(&buffer,NV30_3D_CULL_FACE_ENABLE,1);
    gcm_emit(&buffer,1);

    gcm_emit_method(&buffer,NV30_3D_CULL_FACE,1);
    switch(s -> polygon.cullFace) {
    case RSXGL_CULL_FRONT:
      gcm_emit(&buffer,NV30_3D_CULL_FACE_FRONT);
      break;
    case RSXGL_CULL_BACK:
      gcm_emit(&buffer,NV30_3D_CULL_FACE_BACK);
      break;
    case RSXGL_CULL_FRONT_AND_BACK:
      gcm_emit(&buffer,NV30_3D_CULL_FACE_FRONT_AND_BACK);
      break;
    default:
      gcm_emit(&buffer,0);
      break;
    };
  }
  else {
    buffer = gcm_reserve(context,2);

    gcm_emit_method(&buffer,NV30_3D_CULL_FACE_ENABLE,1);
    gcm_emit(&buffer,0);
  }

  gcm_finish_commands(context,&buffer);

  // other polygon attributes:
  //
  // polygon winding mode:
  {
    buffer = gcm_reserve(context,2);

    gcm_emit_method_at(buffer,0,NV30_3D_FRONT_FACE,1);
    gcm_emit_at(buffer,1,s -> polygon.frontFace == RSXGL_FACE_CW ? NV30_3D_FRONT_FACE_CW : NV30_3D_FRONT_FACE_CCW);

    gcm_finish_commands(context,&buffer);
  }

  // polygon fill mode:
  {
    buffer = gcm_reserve(context,4);

    const uint32_t mode = s -> polygon.mode;
    const uint32_t nv40_mode = (mode == RSXGL_POLYGON_MODE_POINT) ? NV30_3D_POLYGON_MODE_FRONT_POINT : ((mode == RSXGL_POLYGON_MODE_LINE) ? NV30_3D_POLYGON_MODE_FRONT_LINE : NV30_3D_POLYGON_MODE_FRONT_FILL);

    gcm_emit_method_at(buffer,0,NV30_3D_POLYGON_MODE_FRONT,1);
    gcm_emit_at(buffer,1,nv40_mode);

    gcm_emit_method_at(buffer,2,NV30_3D_POLYGON_MODE_BACK,1);
    gcm_emit_at(buffer,3,nv40_mode);

    gcm_finish_n_commands(context,4);
  }

  // polygon offset:
  {
    buffer = gcm_reserve(context,3);

    gcm_emit_method_at(buffer,0,NV30_3D_POLYGON_OFFSET_FACTOR,2);
    gcm_emit_at(buffer,1,_ieee32_t(s -> polygon.offsetFactor).u);
    gcm_emit_at(buffer,2,_ieee32_t(s -> polygon.offsetUnits).u);

    gcm_finish_n_commands(context,3);
  }

  // primitive restart:
  {
    if(s -> enable.primitive_restart) {
      buffer = gcm_reserve(context,4);

      gcm_emit_method_at(buffer,0,0x1dac,1);
      gcm_emit_at(buffer,1,1);

      gcm_emit_method_at(buffer,2,0x1db0,1);
      gcm_emit_at(buffer,3,s -> primitiveRestartIndex);

      gcm_finish_n_commands(context,4);
    }
    else {
      buffer = gcm_reserve(context,2);
      gcm_emit_method_at(buffer,0,0x1dac,1);
      gcm_emit_at(buffer,1,0);
      gcm_finish_n_commands(context,2);
    }
  }

  // line width
  {
    buffer = gcm_reserve(context,2);

    // fixed-point:
    const uint32_t lineWidth = (uint32_t)(s -> lineWidth * (1 << 3)) & ((1 << 9) - 1);

    gcm_emit_method_at(buffer,0,NV30_3D_LINE_WIDTH,1);
    gcm_emit_at(buffer,1,lineWidth);
    
    gcm_finish_n_commands(context,2);
  }

  // point size
  {
    buffer = gcm_reserve(context,2);

    gcm_emit_method_at(buffer,0,NV30_3D_POINT_SIZE,1);
    gcm_emit_at(buffer,1,_ieee32_t(s -> pointSize).u);
    
    gcm_finish_n_commands(context,2);
  }

  s -> invalid.parts.the_rest = 0;
}

//
static inline float
clampf(float x)
{
  if(x < 0) {
    return 0.0f;
  }
  else if(x > 1.0) {
    return 1.0f;
  }
  else {
    return x;
  }
}

GLAPI void APIENTRY
glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
  struct rsxgl_context_t * ctx = current_ctx();

  ctx -> state.viewport.x = x;
  ctx -> state.viewport.y = y;
  ctx -> state.viewport.width = width;
  ctx -> state.viewport.height = height;

  ctx -> state.invalid.parts.viewport = 1;

  RSXGL_NOERROR_();
}

GLAPI void APIENTRY
glDepthRangef(GLclampf zNear, GLclampf zFar)
{
  struct rsxgl_context_t * ctx = current_ctx();

  ctx -> state.viewport.depthRange[0] = clampf(zNear);
  ctx -> state.viewport.depthRange[1] = clampf(zFar);

  ctx -> state.invalid.parts.depth_range = 1;

  RSXGL_NOERROR_();
}

GLAPI void APIENTRY
glColorMask(GLboolean red,GLboolean green,GLboolean blue,GLboolean alpha)
{
  struct rsxgl_context_t * ctx = current_ctx();

  ctx -> state.write_mask.r = red;
  ctx -> state.write_mask.g = green;
  ctx -> state.write_mask.b = blue;
  ctx -> state.write_mask.a = alpha;

  ctx -> state.invalid.parts.color_write_mask = 1;

  RSXGL_NOERROR_();
}

GLAPI void APIENTRY
glDepthMask (GLboolean flag)
{
  struct rsxgl_context_t * ctx = current_ctx();

  ctx -> state.write_mask.depth = flag;

  ctx -> state.invalid.parts.depth_write_mask = 1;

  RSXGL_NOERROR_();
}

GLAPI void APIENTRY
glScissor (GLint x, GLint y, GLsizei width, GLsizei height)
{
  struct rsxgl_context_t * ctx = current_ctx();

  ctx -> state.scissor.x = x;
  ctx -> state.scissor.y = y;
  ctx -> state.scissor.width = width;
  ctx -> state.scissor.height = height;

  ctx -> state.invalid.parts.scissor = 1;

  RSXGL_NOERROR_();
}

GLAPI void APIENTRY
glDepthFunc (GLenum func)
{
  struct rsxgl_context_t * ctx = current_ctx();

  switch(func) {
  case GL_NEVER:
    ctx -> state.depth.func = RSXGL_NEVER;
    break;
  case GL_LESS:
    ctx -> state.depth.func = RSXGL_LESS;
    break;
  case GL_EQUAL:
    ctx -> state.depth.func = RSXGL_EQUAL;
    break;
  case GL_LEQUAL:
    ctx -> state.depth.func = RSXGL_LEQUAL;
    break;
  case GL_GREATER:
    ctx -> state.depth.func = RSXGL_GREATER;
    break;
  case GL_NOTEQUAL:
    ctx -> state.depth.func = RSXGL_NOTEQUAL;
    break;
  case GL_GEQUAL:
    ctx -> state.depth.func = RSXGL_GEQUAL;
    break;
  case GL_ALWAYS:
    ctx -> state.depth.func = RSXGL_ALWAYS;
    break;
  default:
    RSXGL_ERROR_(GL_INVALID_ENUM);
  };
  
  ctx -> state.invalid.parts.the_rest = 1;

  RSXGL_NOERROR_();
}

GLAPI void APIENTRY
glBlendColor (GLclampf green, GLclampf blue, GLclampf alpha, GLclampf red)
{
  struct rsxgl_context_t * ctx = current_ctx();

  ctx -> state.blend.color =
    (uint32_t)(red * 255.0f) << NV30_3D_BLEND_COLOR_R__SHIFT |
    (uint32_t)(green * 255.0f) << NV30_3D_BLEND_COLOR_G__SHIFT |
    (uint32_t)(blue * 255.0f) << NV30_3D_BLEND_COLOR_B__SHIFT |
    (uint32_t)(alpha * 255.0f) << NV30_3D_BLEND_COLOR_A__SHIFT;

  ctx -> state.invalid.parts.the_rest = 1;

  RSXGL_NOERROR_();
}

GLAPI void APIENTRY
glBlendEquation ( GLenum mode )
{
  struct rsxgl_context_t * ctx = current_ctx();

  switch(mode) {
  case GL_FUNC_ADD:
    ctx -> state.blend.rgb_equation = RSXGL_FUNC_ADD;
    ctx -> state.blend.alpha_equation = RSXGL_FUNC_ADD;
    break;
  case GL_MIN:
    ctx -> state.blend.rgb_equation = RSXGL_FUNC_MIN;
    ctx -> state.blend.alpha_equation = RSXGL_FUNC_MIN;
    break;
  case GL_MAX:
    ctx -> state.blend.rgb_equation = RSXGL_FUNC_MAX;
    ctx -> state.blend.alpha_equation = RSXGL_FUNC_MAX;
    break;
  case GL_FUNC_SUBTRACT:
    ctx -> state.blend.rgb_equation = RSXGL_FUNC_SUBTRACT;
    ctx -> state.blend.alpha_equation = RSXGL_FUNC_SUBTRACT;
    break;
  case GL_FUNC_REVERSE_SUBTRACT:
    ctx -> state.blend.rgb_equation = RSXGL_FUNC_REVERSE_SUBTRACT;
    ctx -> state.blend.alpha_equation = RSXGL_FUNC_REVERSE_SUBTRACT;
    break;
  default:
    RSXGL_ERROR_(GL_INVALID_ENUM);
  };

  ctx -> state.invalid.parts.the_rest = 1;

  RSXGL_NOERROR_();
}

GLAPI void APIENTRY
glBlendEquationSeparate (GLenum modeRGB, GLenum modeAlpha)
{
  struct rsxgl_context_t * ctx = current_ctx();

  switch(modeRGB) {
  case GL_FUNC_ADD:
    ctx -> state.blend.rgb_equation = RSXGL_FUNC_ADD;
    break;
  case GL_MIN:
    ctx -> state.blend.rgb_equation = RSXGL_FUNC_MIN;
    break;
  case GL_MAX:
    ctx -> state.blend.rgb_equation = RSXGL_FUNC_MAX;
    break;
  case GL_FUNC_SUBTRACT:
    ctx -> state.blend.rgb_equation = RSXGL_FUNC_SUBTRACT;
    break;
  case GL_FUNC_REVERSE_SUBTRACT:
    ctx -> state.blend.rgb_equation = RSXGL_FUNC_REVERSE_SUBTRACT;
    break;
  default:
    RSXGL_ERROR_(GL_INVALID_ENUM);
  };

  switch(modeAlpha) {
  case GL_FUNC_ADD:
    ctx -> state.blend.alpha_equation = RSXGL_FUNC_ADD;
    break;
  case GL_MIN:
    ctx -> state.blend.alpha_equation = RSXGL_FUNC_MIN;
    break;
  case GL_MAX:
    ctx -> state.blend.alpha_equation = RSXGL_FUNC_MAX;
    break;
  case GL_FUNC_SUBTRACT:
    ctx -> state.blend.alpha_equation = RSXGL_FUNC_SUBTRACT;
    break;
  case GL_FUNC_REVERSE_SUBTRACT:
    ctx -> state.blend.alpha_equation = RSXGL_FUNC_REVERSE_SUBTRACT;
    break;
  default:
    RSXGL_ERROR_(GL_INVALID_ENUM);
  };

  ctx -> state.invalid.parts.the_rest = 1;

  RSXGL_NOERROR_();
}

GLAPI void APIENTRY
glBlendFunc (GLenum _sfactor, GLenum _dfactor)
{
  struct rsxgl_context_t * ctx = current_ctx();

  switch(_sfactor) {
  case GL_ZERO:
    ctx -> state.blend.src_rgb_func = RSXGL_ZERO;
    ctx -> state.blend.src_alpha_func = RSXGL_ZERO;
    break;
  case GL_ONE:
    ctx -> state.blend.src_rgb_func = RSXGL_ONE;
    ctx -> state.blend.src_alpha_func = RSXGL_ONE;
    break;

  case GL_SRC_COLOR:
    ctx -> state.blend.src_rgb_func = RSXGL_SRC_COLOR;
    ctx -> state.blend.src_alpha_func = RSXGL_SRC_COLOR;
    break;
  case GL_ONE_MINUS_SRC_COLOR:
    ctx -> state.blend.src_rgb_func = RSXGL_ONE_MINUS_SRC_COLOR;
    ctx -> state.blend.src_alpha_func = RSXGL_ONE_MINUS_SRC_COLOR;
    break;
  case GL_DST_COLOR:
    ctx -> state.blend.src_rgb_func = RSXGL_DST_COLOR;
    ctx -> state.blend.src_alpha_func = RSXGL_DST_COLOR;
    break;
  case GL_ONE_MINUS_DST_COLOR:
    ctx -> state.blend.src_rgb_func = RSXGL_ONE_MINUS_DST_COLOR;
    ctx -> state.blend.src_alpha_func = RSXGL_ONE_MINUS_DST_COLOR;
    break;

  case GL_SRC_ALPHA:
    ctx -> state.blend.src_rgb_func = RSXGL_SRC_ALPHA;
    ctx -> state.blend.src_alpha_func = RSXGL_SRC_ALPHA;
    break;
  case GL_ONE_MINUS_SRC_ALPHA:
    ctx -> state.blend.src_rgb_func = RSXGL_ONE_MINUS_SRC_ALPHA;
    ctx -> state.blend.src_alpha_func = RSXGL_ONE_MINUS_SRC_ALPHA;
    break;
  case GL_DST_ALPHA:
    ctx -> state.blend.src_rgb_func = RSXGL_DST_ALPHA;
    ctx -> state.blend.src_alpha_func = RSXGL_DST_ALPHA;
    break;
  case GL_ONE_MINUS_DST_ALPHA:
    ctx -> state.blend.src_rgb_func = RSXGL_ONE_MINUS_DST_ALPHA;
    ctx -> state.blend.src_alpha_func = RSXGL_ONE_MINUS_DST_ALPHA;
    break;

  case GL_CONSTANT_COLOR:
    ctx -> state.blend.src_rgb_func = RSXGL_CONSTANT_COLOR;
    ctx -> state.blend.src_alpha_func = RSXGL_CONSTANT_COLOR;
    break;
  case GL_ONE_MINUS_CONSTANT_COLOR:
    ctx -> state.blend.src_rgb_func = RSXGL_ONE_MINUS_CONSTANT_COLOR;
    ctx -> state.blend.src_alpha_func = RSXGL_ONE_MINUS_CONSTANT_COLOR;
    break;
  case GL_CONSTANT_ALPHA:
    ctx -> state.blend.src_rgb_func = RSXGL_CONSTANT_ALPHA;
    ctx -> state.blend.src_alpha_func = RSXGL_CONSTANT_ALPHA;
    break;
  case GL_ONE_MINUS_CONSTANT_ALPHA:
    ctx -> state.blend.src_rgb_func = RSXGL_ONE_MINUS_CONSTANT_ALPHA;
    ctx -> state.blend.src_alpha_func = RSXGL_ONE_MINUS_CONSTANT_ALPHA;
    break;

  case GL_SRC_ALPHA_SATURATE:
    ctx -> state.blend.src_rgb_func = RSXGL_SRC_ALPHA_SATURATE;
    ctx -> state.blend.src_alpha_func = RSXGL_SRC_ALPHA_SATURATE;
    break;

  default:
    RSXGL_ERROR_(GL_INVALID_ENUM);
  };

  switch(_dfactor) {
  case GL_ZERO:
    ctx -> state.blend.dst_rgb_func = RSXGL_ZERO;
    ctx -> state.blend.dst_alpha_func = RSXGL_ZERO;
    break;
  case GL_ONE:
    ctx -> state.blend.dst_rgb_func = RSXGL_ONE;
    ctx -> state.blend.dst_alpha_func = RSXGL_ONE;
    break;

  case GL_SRC_COLOR:
    ctx -> state.blend.dst_rgb_func = RSXGL_SRC_COLOR;
    ctx -> state.blend.dst_alpha_func = RSXGL_SRC_COLOR;
    break;
  case GL_ONE_MINUS_SRC_COLOR:
    ctx -> state.blend.dst_rgb_func = RSXGL_ONE_MINUS_SRC_COLOR;
    ctx -> state.blend.dst_alpha_func = RSXGL_ONE_MINUS_SRC_COLOR;
    break;
  case GL_DST_COLOR:
    ctx -> state.blend.dst_rgb_func = RSXGL_DST_COLOR;
    ctx -> state.blend.dst_alpha_func = RSXGL_DST_COLOR;
    break;
  case GL_ONE_MINUS_DST_COLOR:
    ctx -> state.blend.dst_rgb_func = RSXGL_ONE_MINUS_DST_COLOR;
    ctx -> state.blend.dst_alpha_func = RSXGL_ONE_MINUS_DST_COLOR;
    break;

  case GL_SRC_ALPHA:
    ctx -> state.blend.dst_rgb_func = RSXGL_SRC_ALPHA;
    ctx -> state.blend.dst_alpha_func = RSXGL_SRC_ALPHA;
    break;
  case GL_ONE_MINUS_SRC_ALPHA:
    ctx -> state.blend.dst_rgb_func = RSXGL_ONE_MINUS_SRC_ALPHA;
    ctx -> state.blend.dst_alpha_func = RSXGL_ONE_MINUS_SRC_ALPHA;
    break;
  case GL_DST_ALPHA:
    ctx -> state.blend.dst_rgb_func = RSXGL_DST_ALPHA;
    ctx -> state.blend.dst_alpha_func = RSXGL_DST_ALPHA;
    break;
  case GL_ONE_MINUS_DST_ALPHA:
    ctx -> state.blend.dst_rgb_func = RSXGL_ONE_MINUS_DST_ALPHA;
    ctx -> state.blend.dst_alpha_func = RSXGL_ONE_MINUS_DST_ALPHA;
    break;

  case GL_CONSTANT_COLOR:
    ctx -> state.blend.dst_rgb_func = RSXGL_CONSTANT_COLOR;
    ctx -> state.blend.dst_alpha_func = RSXGL_CONSTANT_COLOR;
    break;
  case GL_ONE_MINUS_CONSTANT_COLOR:
    ctx -> state.blend.dst_rgb_func = RSXGL_ONE_MINUS_CONSTANT_COLOR;
    ctx -> state.blend.dst_alpha_func = RSXGL_ONE_MINUS_CONSTANT_COLOR;
    break;
  case GL_CONSTANT_ALPHA:
    ctx -> state.blend.dst_rgb_func = RSXGL_CONSTANT_ALPHA;
    ctx -> state.blend.dst_alpha_func = RSXGL_CONSTANT_ALPHA;
    break;
  case GL_ONE_MINUS_CONSTANT_ALPHA:
    ctx -> state.blend.dst_rgb_func = RSXGL_ONE_MINUS_CONSTANT_ALPHA;
    ctx -> state.blend.dst_alpha_func = RSXGL_ONE_MINUS_CONSTANT_ALPHA;
    break;

  default:
    RSXGL_ERROR_(GL_INVALID_ENUM);
  };

  ctx -> state.invalid.parts.the_rest = 1;

  RSXGL_NOERROR_();
}

GLAPI void APIENTRY
glBlendFuncSeparate (GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha)
{
  struct rsxgl_context_t * ctx = current_ctx();

  switch(srcRGB) {
  case GL_ZERO:
    ctx -> state.blend.src_rgb_func = RSXGL_ZERO;
    break;
  case GL_ONE:
    ctx -> state.blend.src_rgb_func = RSXGL_ONE;
    break;

  case GL_SRC_COLOR:
    ctx -> state.blend.src_rgb_func = RSXGL_SRC_COLOR;
    break;
  case GL_ONE_MINUS_SRC_COLOR:
    ctx -> state.blend.src_rgb_func = RSXGL_ONE_MINUS_SRC_COLOR;
    break;
  case GL_DST_COLOR:
    ctx -> state.blend.src_rgb_func = RSXGL_DST_COLOR;
    break;
  case GL_ONE_MINUS_DST_COLOR:
    ctx -> state.blend.src_rgb_func = RSXGL_ONE_MINUS_DST_COLOR;
    break;

  case GL_SRC_ALPHA:
    ctx -> state.blend.src_rgb_func = RSXGL_SRC_ALPHA;
    break;
  case GL_ONE_MINUS_SRC_ALPHA:
    ctx -> state.blend.src_rgb_func = RSXGL_ONE_MINUS_SRC_ALPHA;
    break;
  case GL_DST_ALPHA:
    ctx -> state.blend.src_rgb_func = RSXGL_DST_ALPHA;
    break;
  case GL_ONE_MINUS_DST_ALPHA:
    ctx -> state.blend.src_rgb_func = RSXGL_ONE_MINUS_DST_ALPHA;
    break;

  case GL_CONSTANT_COLOR:
    ctx -> state.blend.src_rgb_func = RSXGL_CONSTANT_COLOR;
    break;
  case GL_ONE_MINUS_CONSTANT_COLOR:
    ctx -> state.blend.src_rgb_func = RSXGL_ONE_MINUS_CONSTANT_COLOR;
    break;
  case GL_CONSTANT_ALPHA:
    ctx -> state.blend.src_rgb_func = RSXGL_CONSTANT_ALPHA;
    break;
  case GL_ONE_MINUS_CONSTANT_ALPHA:
    ctx -> state.blend.src_rgb_func = RSXGL_ONE_MINUS_CONSTANT_ALPHA;
    break;

  case GL_SRC_ALPHA_SATURATE:
    ctx -> state.blend.src_rgb_func = RSXGL_SRC_ALPHA_SATURATE;
    break;

  default:
    RSXGL_ERROR_(GL_INVALID_ENUM);
  };

  switch(dstRGB) {
  case GL_ZERO:
    ctx -> state.blend.dst_rgb_func = RSXGL_ZERO;
    break;
  case GL_ONE:
    ctx -> state.blend.dst_rgb_func = RSXGL_ONE;
    break;

  case GL_SRC_COLOR:
    ctx -> state.blend.dst_rgb_func = RSXGL_SRC_COLOR;
    break;
  case GL_ONE_MINUS_SRC_COLOR:
    ctx -> state.blend.dst_rgb_func = RSXGL_ONE_MINUS_SRC_COLOR;
    break;
  case GL_DST_COLOR:
    ctx -> state.blend.dst_rgb_func = RSXGL_DST_COLOR;
    break;
  case GL_ONE_MINUS_DST_COLOR:
    ctx -> state.blend.dst_rgb_func = RSXGL_ONE_MINUS_DST_COLOR;
    break;

  case GL_SRC_ALPHA:
    ctx -> state.blend.dst_rgb_func = RSXGL_SRC_ALPHA;
    break;
  case GL_ONE_MINUS_SRC_ALPHA:
    ctx -> state.blend.dst_rgb_func = RSXGL_ONE_MINUS_SRC_ALPHA;
    break;
  case GL_DST_ALPHA:
    ctx -> state.blend.dst_rgb_func = RSXGL_DST_ALPHA;
    break;
  case GL_ONE_MINUS_DST_ALPHA:
    ctx -> state.blend.dst_rgb_func = RSXGL_ONE_MINUS_DST_ALPHA;
    break;

  case GL_CONSTANT_COLOR:
    ctx -> state.blend.dst_rgb_func = RSXGL_CONSTANT_COLOR;
    break;
  case GL_ONE_MINUS_CONSTANT_COLOR:
    ctx -> state.blend.dst_rgb_func = RSXGL_ONE_MINUS_CONSTANT_COLOR;
    break;
  case GL_CONSTANT_ALPHA:
    ctx -> state.blend.dst_rgb_func = RSXGL_CONSTANT_ALPHA;
    break;
  case GL_ONE_MINUS_CONSTANT_ALPHA:
    ctx -> state.blend.dst_rgb_func = RSXGL_ONE_MINUS_CONSTANT_ALPHA;
    break;

  default:
    RSXGL_ERROR_(GL_INVALID_ENUM);
  };

  //
  switch(srcAlpha) {
  case GL_ZERO:
    ctx -> state.blend.src_alpha_func = RSXGL_ZERO;
    break;
  case GL_ONE:
    ctx -> state.blend.src_alpha_func = RSXGL_ONE;
    break;

  case GL_SRC_COLOR:
    ctx -> state.blend.src_alpha_func = RSXGL_SRC_COLOR;
    break;
  case GL_ONE_MINUS_SRC_COLOR:
    ctx -> state.blend.src_alpha_func = RSXGL_ONE_MINUS_SRC_COLOR;
    break;
  case GL_DST_COLOR:
    ctx -> state.blend.src_alpha_func = RSXGL_DST_COLOR;
    break;
  case GL_ONE_MINUS_DST_COLOR:
    ctx -> state.blend.src_alpha_func = RSXGL_ONE_MINUS_DST_COLOR;
    break;

  case GL_SRC_ALPHA:
    ctx -> state.blend.src_alpha_func = RSXGL_SRC_ALPHA;
    break;
  case GL_ONE_MINUS_SRC_ALPHA:
    ctx -> state.blend.src_alpha_func = RSXGL_ONE_MINUS_SRC_ALPHA;
    break;
  case GL_DST_ALPHA:
    ctx -> state.blend.src_alpha_func = RSXGL_DST_ALPHA;
    break;
  case GL_ONE_MINUS_DST_ALPHA:
    ctx -> state.blend.src_alpha_func = RSXGL_ONE_MINUS_DST_ALPHA;
    break;

  case GL_CONSTANT_COLOR:
    ctx -> state.blend.src_alpha_func = RSXGL_CONSTANT_COLOR;
    break;
  case GL_ONE_MINUS_CONSTANT_COLOR:
    ctx -> state.blend.src_alpha_func = RSXGL_ONE_MINUS_CONSTANT_COLOR;
    break;
  case GL_CONSTANT_ALPHA:
    ctx -> state.blend.src_alpha_func = RSXGL_CONSTANT_ALPHA;
    break;
  case GL_ONE_MINUS_CONSTANT_ALPHA:
    ctx -> state.blend.src_alpha_func = RSXGL_ONE_MINUS_CONSTANT_ALPHA;
    break;

  case GL_SRC_ALPHA_SATURATE:
    ctx -> state.blend.src_alpha_func = RSXGL_SRC_ALPHA_SATURATE;
    break;

  default:
    RSXGL_ERROR_(GL_INVALID_ENUM);
  };

  switch(dstAlpha) {
  case GL_ZERO:
    ctx -> state.blend.dst_alpha_func = RSXGL_ZERO;
    break;
  case GL_ONE:
    ctx -> state.blend.dst_alpha_func = RSXGL_ONE;
    break;

  case GL_SRC_COLOR:
    ctx -> state.blend.dst_alpha_func = RSXGL_SRC_COLOR;
    break;
  case GL_ONE_MINUS_SRC_COLOR:
    ctx -> state.blend.dst_alpha_func = RSXGL_ONE_MINUS_SRC_COLOR;
    break;
  case GL_DST_COLOR:
    ctx -> state.blend.dst_alpha_func = RSXGL_DST_COLOR;
    break;
  case GL_ONE_MINUS_DST_COLOR:
    ctx -> state.blend.dst_alpha_func = RSXGL_ONE_MINUS_DST_COLOR;
    break;

  case GL_SRC_ALPHA:
    ctx -> state.blend.dst_alpha_func = RSXGL_SRC_ALPHA;
    break;
  case GL_ONE_MINUS_SRC_ALPHA:
    ctx -> state.blend.dst_alpha_func = RSXGL_ONE_MINUS_SRC_ALPHA;
    break;
  case GL_DST_ALPHA:
    ctx -> state.blend.dst_alpha_func = RSXGL_DST_ALPHA;
    break;
  case GL_ONE_MINUS_DST_ALPHA:
    ctx -> state.blend.dst_alpha_func = RSXGL_ONE_MINUS_DST_ALPHA;
    break;

  case GL_CONSTANT_COLOR:
    ctx -> state.blend.dst_alpha_func = RSXGL_CONSTANT_COLOR;
    break;
  case GL_ONE_MINUS_CONSTANT_COLOR:
    ctx -> state.blend.dst_alpha_func = RSXGL_ONE_MINUS_CONSTANT_COLOR;
    break;
  case GL_CONSTANT_ALPHA:
    ctx -> state.blend.dst_alpha_func = RSXGL_CONSTANT_ALPHA;
    break;
  case GL_ONE_MINUS_CONSTANT_ALPHA:
    ctx -> state.blend.dst_alpha_func = RSXGL_ONE_MINUS_CONSTANT_ALPHA;
    break;

  default:
    RSXGL_ERROR_(GL_INVALID_ENUM);
  };

  ctx -> state.invalid.parts.the_rest = 1;

  RSXGL_NOERROR_();
}

GLAPI void APIENTRY
glStencilFuncSeparate (GLenum face, GLenum func, GLint ref, GLuint mask)
{
  struct rsxgl_context_t * ctx = current_ctx();

  if(!(face == GL_FRONT || face == GL_BACK || face == GL_FRONT_AND_BACK)) {
    RSXGL_ERROR_(GL_INVALID_ENUM);
  }

  unsigned int faces = ((face == GL_FRONT || face == GL_FRONT_AND_BACK) ? 1 : 0) | ((face == GL_BACK || face == GL_FRONT_AND_BACK) ? 2 : 0);

  switch(func) {
  case GL_NEVER:
    if(faces & 1) ctx -> state.stencil.face[0].func = RSXGL_NEVER;
    if(faces & 2) ctx -> state.stencil.face[1].func = RSXGL_NEVER;
    break;
  case GL_LESS:
    if(faces & 1) ctx -> state.stencil.face[0].func = RSXGL_LESS;
    if(faces & 2) ctx -> state.stencil.face[1].func = RSXGL_LESS;
    break;
  case GL_EQUAL:
    if(faces & 1) ctx -> state.stencil.face[0].func = RSXGL_EQUAL;
    if(faces & 2) ctx -> state.stencil.face[1].func = RSXGL_EQUAL;
    break;
  case GL_LEQUAL:
    if(faces & 1) ctx -> state.stencil.face[0].func = RSXGL_LEQUAL;
    if(faces & 2) ctx -> state.stencil.face[1].func = RSXGL_LEQUAL;
    break;
  case GL_GREATER:
    if(faces & 1) ctx -> state.stencil.face[0].func = RSXGL_GREATER;
    if(faces & 2) ctx -> state.stencil.face[1].func = RSXGL_GREATER;
    break;
  case GL_NOTEQUAL:
    if(faces & 1) ctx -> state.stencil.face[0].func = RSXGL_NOTEQUAL;
    if(faces & 2) ctx -> state.stencil.face[1].func = RSXGL_NOTEQUAL;
    break;
  case GL_GEQUAL:
    if(faces & 1) ctx -> state.stencil.face[0].func = RSXGL_GEQUAL;
    if(faces & 2) ctx -> state.stencil.face[1].func = RSXGL_GEQUAL;
    break;
  case GL_ALWAYS:
    if(faces & 1) ctx -> state.stencil.face[0].func = RSXGL_ALWAYS;
    if(faces & 2) ctx -> state.stencil.face[1].func = RSXGL_ALWAYS;
    break;

  default:
    RSXGL_ERROR_(GL_INVALID_ENUM);
  };

  if(faces & 1) {
    ctx -> state.stencil.face[0].ref = ref;
    ctx -> state.stencil.face[0].mask = mask;
  }
  if(faces & 2) {
    ctx -> state.stencil.face[1].ref = ref;
    ctx -> state.stencil.face[1].mask = mask;
  }

  ctx -> state.invalid.parts.the_rest = 1;

  RSXGL_NOERROR_();
}

GLAPI void APIENTRY
glStencilFunc (GLenum func, GLint ref, GLuint mask)
{
  glStencilFuncSeparate(GL_FRONT_AND_BACK,func,ref,mask);
}

GLAPI void APIENTRY
glStencilMaskSeparate (GLenum face, GLuint mask)
{
  struct rsxgl_context_t * ctx = current_ctx();

  if(!(face == GL_FRONT || face == GL_BACK || face == GL_FRONT_AND_BACK)) {
    RSXGL_ERROR_(GL_INVALID_ENUM);
  }

  unsigned int faces = ((face == GL_FRONT || face == GL_FRONT_AND_BACK) ? 1 : 0) | ((face == GL_BACK || face == GL_FRONT_AND_BACK) ? 2 : 0);

  if(faces & 1) {
    ctx -> state.stencil.face[0].writemask = mask;
  }
  if(faces & 2) {
    ctx -> state.stencil.face[1].writemask = mask;
  }

  ctx -> state.invalid.parts.the_rest = 1;

  RSXGL_NOERROR_();
}

GLAPI void APIENTRY
glStencilMask (GLuint mask)
{
  glStencilMaskSeparate(GL_FRONT_AND_BACK,mask);
}

static inline unsigned int
rsxgl_stencil_op(GLenum op)
{
  switch(op) {
  case GL_ZERO:
    return RSXGL_ZERO;
  case GL_KEEP:
    return RSXGL_KEEP;
  case GL_REPLACE:
    return RSXGL_REPLACE;
  case GL_INCR:
    return RSXGL_INCR;
  case GL_DECR:
    return RSXGL_DECR;
  case GL_INVERT:
    return RSXGL_INVERT;
  case GL_INCR_WRAP:
    return RSXGL_INCR_WRAP;
  case GL_DECR_WRAP:
    return RSXGL_DECR_WRAP;
  default:
    RSXGL_ERROR(GL_INVALID_ENUM,GL_ZERO);
  };
}

GLAPI void APIENTRY
glStencilOpSeparate (GLenum face, GLenum fail, GLenum zfail, GLenum zpass)
{
  struct rsxgl_context_t * ctx = current_ctx();

  if(face == GL_FRONT || face == GL_FRONT_AND_BACK) {
    ctx -> state.stencil.face[0].fail_op = rsxgl_stencil_op(fail);
    ctx -> state.stencil.face[0].zfail_op = rsxgl_stencil_op(zfail);
    ctx -> state.stencil.face[0].pass_op = rsxgl_stencil_op(zpass);
  }
  else if(face == GL_BACK || face == GL_FRONT_AND_BACK) {
    ctx -> state.stencil.face[1].fail_op = rsxgl_stencil_op(fail);
    ctx -> state.stencil.face[1].zfail_op = rsxgl_stencil_op(zfail);
    ctx -> state.stencil.face[1].pass_op = rsxgl_stencil_op(zpass);
  }
  else {
    RSXGL_ERROR_(GL_INVALID_ENUM);
  }

  ctx -> state.invalid.parts.the_rest = 1;

  RSXGL_NOERROR_();
}

GLAPI void APIENTRY
glStencilOp (GLenum fail, GLenum zfail, GLenum zpass)
{
  glStencilOpSeparate(GL_FRONT_AND_BACK,fail,zfail,zpass);
}

GLAPI void APIENTRY
glCullFace (GLenum mode)
{
  struct rsxgl_context_t * ctx = current_ctx();

  switch(mode) {
  case GL_FRONT:
    ctx -> state.polygon.cullFace = RSXGL_CULL_FRONT;
    break;
  case GL_BACK:
    ctx -> state.polygon.cullFace = RSXGL_CULL_BACK;
    break;
  case GL_FRONT_AND_BACK:
    ctx -> state.polygon.cullFace = RSXGL_CULL_FRONT_AND_BACK;
    break;
  default:
    RSXGL_ERROR_(GL_INVALID_ENUM);
  };

  ctx -> state.invalid.parts.the_rest = 1;
}

GLAPI void APIENTRY
glFrontFace (GLenum mode)
{
  struct rsxgl_context_t * ctx = current_ctx();

  switch(mode) {
  case GL_CW:
    ctx -> state.polygon.frontFace = RSXGL_FACE_CW;
    break;
  case GL_CCW:
    ctx -> state.polygon.frontFace = RSXGL_FACE_CCW;
    break;
  default:
    RSXGL_ERROR_(GL_INVALID_ENUM);
  };

  ctx -> state.invalid.parts.the_rest = 1;
}

GLAPI void APIENTRY
glLineWidth (GLfloat width)
{
  struct rsxgl_context_t * ctx = current_ctx();
  ctx -> state.lineWidth = width;
  ctx -> state.invalid.parts.the_rest = 1;
}

GLAPI void APIENTRY
glPointSize (GLfloat size)
{
  struct rsxgl_context_t * ctx = current_ctx();
  ctx -> state.pointSize = size;
  ctx -> state.invalid.parts.the_rest = 1;
}

GLAPI void APIENTRY
glPolygonMode (GLenum face, GLenum mode)
{
  struct rsxgl_context_t * ctx = current_ctx();

  if(face != GL_FRONT_AND_BACK) {
    RSXGL_ERROR_(GL_INVALID_ENUM);
  }

  switch(mode) {
  case GL_POINT:
    ctx -> state.polygon.mode = RSXGL_POLYGON_MODE_POINT;
    break;
  case GL_LINE:
    ctx -> state.polygon.mode = RSXGL_POLYGON_MODE_LINE;
    break;
  case GL_FILL:
    ctx -> state.polygon.mode = RSXGL_POLYGON_MODE_FILL;
    break;
  default:
    RSXGL_ERROR_(GL_INVALID_ENUM);
  };

  ctx -> state.invalid.parts.the_rest = 1;
}

GLAPI void APIENTRY
glPolygonOffset (GLfloat factor, GLfloat units)
{
  struct rsxgl_context_t * ctx = current_ctx();

  ctx -> state.polygon.offsetFactor = factor;
  ctx -> state.polygon.offsetUnits = units;

  ctx -> state.invalid.parts.the_rest = 1;
}


static inline void
rsxgl_pixel_store(rsxgl_context_t * ctx,GLenum pname,uint32_t param)
{
  if(!(pname == GL_PACK_SWAP_BYTES ||
       pname == GL_PACK_LSB_FIRST ||
       pname == GL_PACK_ROW_LENGTH ||
       pname == GL_PACK_IMAGE_HEIGHT ||
       pname == GL_PACK_SKIP_PIXELS ||
       pname == GL_PACK_SKIP_ROWS ||
       pname == GL_PACK_SKIP_IMAGES ||
       pname == GL_PACK_ALIGNMENT ||
       pname == GL_UNPACK_SWAP_BYTES ||
       pname == GL_UNPACK_LSB_FIRST ||
       pname == GL_UNPACK_ROW_LENGTH ||
       pname == GL_UNPACK_IMAGE_HEIGHT ||
       pname == GL_UNPACK_SKIP_PIXELS ||
       pname == GL_UNPACK_SKIP_ROWS ||
       pname == GL_UNPACK_ALIGNMENT)) {
    RSXGL_ERROR_(GL_INVALID_ENUM);
  }

  if(pname == GL_PACK_SWAP_BYTES) {
    ctx -> state.pixel_store.pack_swap_bytes = param;
  }
  else if(pname == GL_PACK_LSB_FIRST) {
    ctx -> state.pixel_store.pack_lsb_first = param;
  }
  else if(pname == GL_PACK_ROW_LENGTH) {
    ctx -> state.pixel_store.pack_row_length = param;
  }
  else if(pname == GL_PACK_IMAGE_HEIGHT) {
    ctx -> state.pixel_store.pack_image_height = param;
  }
  else if(pname == GL_PACK_SKIP_PIXELS) {
    ctx -> state.pixel_store.pack_skip_pixels = param;
  }
  else if(pname == GL_PACK_SKIP_ROWS) {
    ctx -> state.pixel_store.pack_skip_rows = param;
  }
  else if(pname == GL_PACK_SKIP_IMAGES) {
    ctx -> state.pixel_store.pack_skip_images = param;
  }
  else if(pname == GL_PACK_ALIGNMENT) {
    if(param == 1) {
      ctx -> state.pixel_store.pack_alignment = RSXGL_PIXEL_STORE_ALIGNMENT_1;
    }
    else if(param == 2) {
      ctx -> state.pixel_store.pack_alignment = RSXGL_PIXEL_STORE_ALIGNMENT_2;
    }
    else if(param == 4) {
      ctx -> state.pixel_store.pack_alignment = RSXGL_PIXEL_STORE_ALIGNMENT_4;
    }
    else if(param == 8) {
      ctx -> state.pixel_store.pack_alignment = RSXGL_PIXEL_STORE_ALIGNMENT_8;
    }
    else {
      RSXGL_ERROR_(GL_INVALID_VALUE);
    }
  }
  else if(pname == GL_UNPACK_SWAP_BYTES) {
    ctx -> state.pixel_store.unpack_swap_bytes = param;
  }
  else if(pname == GL_UNPACK_LSB_FIRST) {
    ctx -> state.pixel_store.unpack_lsb_first = param;
  }
  else if(pname == GL_UNPACK_ROW_LENGTH) {
    ctx -> state.pixel_store.unpack_row_length = param;
  }
  else if(pname == GL_UNPACK_IMAGE_HEIGHT) {
    ctx -> state.pixel_store.unpack_image_height = param;
  }
  else if(pname == GL_UNPACK_SKIP_PIXELS) {
    ctx -> state.pixel_store.unpack_skip_pixels = param;
  }
  else if(pname == GL_UNPACK_SKIP_ROWS) {
    ctx -> state.pixel_store.unpack_skip_rows = param;
  }
  else if(pname == GL_UNPACK_ALIGNMENT) {
    if(param == 1) {
      ctx -> state.pixel_store.unpack_alignment = RSXGL_PIXEL_STORE_ALIGNMENT_1;
    }
    else if(param == 2) {
      ctx -> state.pixel_store.unpack_alignment = RSXGL_PIXEL_STORE_ALIGNMENT_2;
    }
    else if(param == 4) {
      ctx -> state.pixel_store.unpack_alignment = RSXGL_PIXEL_STORE_ALIGNMENT_4;
    }
    else if(param == 8) {
      ctx -> state.pixel_store.unpack_alignment = RSXGL_PIXEL_STORE_ALIGNMENT_8;
    }
    else {
      RSXGL_ERROR_(GL_INVALID_VALUE);
    }
  }
}

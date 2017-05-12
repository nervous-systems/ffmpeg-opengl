#include "libavutil/opt.h"
#include "internal.h"

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#endif

#include <GLFW/glfw3.h>

static const float position[12] = {
  -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f};

static const GLchar *v_shader_source =
  "attribute vec2 position;\n"
  "varying vec2 tex_coord;\n"
  "void main(void) {\n"
  "  gl_Position = vec4(position, 0, 1);\n"
  "  tex_coord = position * 0.5 + 0.5;\n"
  "}\n";

static const GLchar *f_shader_source =
  "uniform sampler2D tex_y;\n"
  "uniform sampler2D tex_u;\n"
  "uniform sampler2D tex_v;\n"
  "varying vec2 tex_coord;\n"
  "const mat3 bt601_coeff = mat3(1.164,1.164,1.164,0.0,-0.392,2.017,1.596,-0.813,0.0);\n"
  "const vec3 offsets     = vec3(-0.0625, -0.5, -0.5);\n"
  "vec3 sampleRgb(vec2 loc) {\n"
  "  float y = texture2D(tex_y, loc).r;\n"
  "  float u = texture2D(tex_u, loc).r;\n"
  "  float v = texture2D(tex_v, loc).r;\n"
  "  return bt601_coeff * (vec3(y, u, v) + offsets);\n"
  "}\n"
  "void main() {\n"
  "  gl_FragColor = vec4(sampleRgb(tex_coord), 1.);\n"
  "}\n";

#define PIXEL_FORMAT GL_RED

typedef struct {
  const AVClass *class;
  GLuint        program;
  GLuint        tex[3];
  GLuint        pbo_in;
  GLFWwindow    *window;
  GLuint        pos_buf;
} GenericShaderContext;

#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption genericshader_options[] = {{}, {NULL}};

AVFILTER_DEFINE_CLASS(genericshader);

static GLuint build_shader(AVFilterContext *ctx, const GLchar *shader_source, GLenum type) {
  GLuint shader = glCreateShader(type);
  if (!shader || !glIsShader(shader)) {
    return 0;
  }
  glShaderSource(shader, 1, &shader_source, 0);
  glCompileShader(shader);
  GLint status;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
  if (status == GL_TRUE) {
    return shader;
  }

  GLint length;
  glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
  unsigned char* log = (unsigned char*)av_malloc(length);
  glGetShaderInfoLog(shader, length, NULL, log);
  av_log(ctx, AV_LOG_ERROR, "Shader log: %s", log);
  av_free(log);
  return 0;
}

static void vbo_setup(GenericShaderContext *gs) {
  glGenBuffers(1, &gs->pos_buf);
  glBindBuffer(GL_ARRAY_BUFFER, gs->pos_buf);
  glBufferData(GL_ARRAY_BUFFER, sizeof(position), position, GL_STATIC_DRAW);

  GLint loc = glGetAttribLocation(gs->program, "position");
  glEnableVertexAttribArray(loc);
  glVertexAttribPointer(loc, 2, GL_FLOAT, GL_FALSE, 0, 0);
}

static void pbo_setup(AVFilterLink *inlink) {
  AVFilterContext     *ctx = inlink->dst;
  GenericShaderContext *gs = ctx->priv;

  glGenBuffers(1, &gs->pbo_in);

  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, gs->pbo_in);
  glBufferData(GL_PIXEL_UNPACK_BUFFER, inlink->w*inlink->h*1.5, 0, GL_STREAM_DRAW);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
}

static void tex_setup(AVFilterLink *inlink) {
  AVFilterContext     *ctx = inlink->dst;
  GenericShaderContext *gs = ctx->priv;

  glGenTextures(3, gs->tex);

  for(int i = 0; i < 3; i++) {
    glActiveTexture(GL_TEXTURE0 + i);
    glBindTexture(GL_TEXTURE_2D, gs->tex[i]);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8,
                 inlink->w / (i ? 2 : 1),
                 inlink->h / (i ? 2 : 1),
                 0, PIXEL_FORMAT, GL_UNSIGNED_BYTE, NULL);
  }

  glUniform1i(glGetUniformLocation(gs->program, "tex_y"), 0);
  glUniform1i(glGetUniformLocation(gs->program, "tex_u"), 1);
  glUniform1i(glGetUniformLocation(gs->program, "tex_v"), 2);
}

static int build_program(AVFilterContext *ctx) {
  GLuint v_shader, f_shader;
  GenericShaderContext *gs = ctx->priv;

  if ((v_shader = build_shader(ctx, v_shader_source, GL_VERTEX_SHADER)) &&
      (f_shader = build_shader(ctx, f_shader_source, GL_FRAGMENT_SHADER))) {
    gs->program = glCreateProgram();
    glAttachShader(gs->program, v_shader);
    glAttachShader(gs->program, f_shader);
    glLinkProgram(gs->program);

    GLint status;
    glGetProgramiv(gs->program, GL_LINK_STATUS, &status);
    return status == GL_TRUE ? 0 : -1;
  }
  return -1;
}

static av_cold int init(AVFilterContext *ctx) {
  return glfwInit() ? 0 : -1;
}

static int config_props(AVFilterLink *inlink) {
  AVFilterContext     *ctx = inlink->dst;
  GenericShaderContext *gs = ctx->priv;

  glfwWindowHint(GLFW_VISIBLE, 0);
  gs->window = glfwCreateWindow(inlink->w, inlink->h, "", NULL, NULL);

  glfwMakeContextCurrent(gs->window);

  #ifndef __APPLE__
  glewExperimental = GL_TRUE;
  glewInit();
  #endif

  glViewport(0, 0, inlink->w, inlink->h);

  int ret;
  if((ret = build_program(ctx)) < 0) {
    return ret;
  }

  glUseProgram(gs->program);
  pbo_setup(inlink);
  vbo_setup(gs);
  tex_setup(inlink);
  return 0;
}

static void input_frame(AVFilterLink *inlink, AVFrame *in, GLuint pbo) {
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
  glBufferData(GL_PIXEL_UNPACK_BUFFER, inlink->w * inlink->h * 1.5, 0, GL_STREAM_DRAW);
  GLubyte *ptr = glMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY);

  memcpy(ptr, in->data[0], inlink->w * inlink->h);
  ptr += inlink->w * inlink->h;
  memcpy(ptr, in->data[1], inlink->w/2 * inlink->h/2);
  ptr += inlink->w/2 * inlink->h/2;
  memcpy(ptr, in->data[2], inlink->w/2 * inlink->h/2);

  glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in) {
  AVFilterContext     *ctx = inlink->dst;
  GenericShaderContext *gs = ctx->priv;
  AVFilterLink    *outlink = ctx->outputs[0];

  AVFrame *out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
  if (!out) {
    av_frame_free(&in);
    return AVERROR(ENOMEM);
  }
  av_frame_copy_props(out, in);

  input_frame(inlink, in, gs->pbo_in);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, gs->pbo_in);

  int w, h, offset = 0;
  for(int i = 0; i < 3; i++) {
    glActiveTexture(GL_TEXTURE0 + i);
    glBindTexture(GL_TEXTURE_2D, gs->tex[i]);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, in->linesize[i]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                    w = inlink->w / (i ? 2 : 1),
                    h = inlink->h / (i ? 2 : 1),
                    PIXEL_FORMAT, GL_UNSIGNED_BYTE, offset);
    offset += w * h;
  }
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

  glDrawArrays(GL_TRIANGLES, 0, 6);
  glReadPixels(0, 0, outlink->w, outlink->h, GL_RGB, GL_UNSIGNED_BYTE, (GLvoid *)out->data[0]);

  av_frame_free(&in);
  return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx) {
  GenericShaderContext *gs = ctx->priv;
  glDeleteTextures(3, gs->tex);
  glDeleteProgram(gs->program);
  glDeleteBuffers(1, &gs->pos_buf);
  glfwDestroyWindow(gs->window);
}

static int query_formats(AVFilterContext *ctx) {
  AVFilterFormats *formats = NULL;
  int ret;
  if ((ret = ff_add_format(&formats, AV_PIX_FMT_YUV420P)) < 0 ||
      (ret = ff_formats_ref(formats, &ctx->inputs[0]->out_formats)) < 0) {
    return ret;
  }
  formats = NULL;
  if ((ret = ff_add_format(&formats, AV_PIX_FMT_RGB24)) < 0 ||
      (ret = ff_formats_ref(formats, &ctx->outputs[0]->in_formats)) < 0) {
    return ret;
  }
  return 0;
}

static const AVFilterPad genericshader_inputs[] = {
  {.name = "default",
   .type = AVMEDIA_TYPE_VIDEO,
   .config_props = config_props,
   .filter_frame = filter_frame},
  {NULL}};

static const AVFilterPad genericshader_outputs[] = {
  {.name = "default", .type = AVMEDIA_TYPE_VIDEO}, {NULL}};

AVFilter ff_vf_genericshader = {
  .name          = "genericshader",
  .description   = NULL_IF_CONFIG_SMALL("Generic OpenGL shader filter"),
  .priv_size     = sizeof(GenericShaderContext),
  .init          = init,
  .uninit        = uninit,
  .query_formats = query_formats,
  .inputs        = genericshader_inputs,
  .outputs       = genericshader_outputs,
  .priv_class    = &genericshader_class,
  .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC};

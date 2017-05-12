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
  "varying vec2 texCoord;\n"
  "void main(void) {\n"
  "  gl_Position = vec4(position, 0, 1);\n"
  "  texCoord = position;\n"
  "}\n";

static const GLchar *f_shader_source =
  "uniform sampler2D tex;\n"
  "varying vec2 texCoord;\n"
  "void main() {\n"
  "  gl_FragColor = texture2D(tex, texCoord * 0.5 + 0.5);\n"
  "}\n";

#define PIXEL_FORMAT GL_RGB

typedef struct {
  const AVClass *class;
  GLuint        program;
  GLuint        frame_tex;
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
  return status == GL_TRUE ? shader : 0;
}

static void vbo_setup(GenericShaderContext *gs) {
  glGenBuffers(1, &gs->pos_buf);
  glBindBuffer(GL_ARRAY_BUFFER, gs->pos_buf);
  glBufferData(GL_ARRAY_BUFFER, sizeof(position), position, GL_STATIC_DRAW);

  GLint loc = glGetAttribLocation(gs->program, "position");
  glEnableVertexAttribArray(loc);
  glVertexAttribPointer(loc, 2, GL_FLOAT, GL_FALSE, 0, 0);
}

static void tex_setup(AVFilterLink *inlink) {
  AVFilterContext     *ctx = inlink->dst;
  GenericShaderContext *gs = ctx->priv;

  glGenTextures(1, &gs->frame_tex);
  glActiveTexture(GL_TEXTURE0);

  glBindTexture(GL_TEXTURE_2D, gs->frame_tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, inlink->w, inlink->h, 0, PIXEL_FORMAT, GL_UNSIGNED_BYTE, NULL);

  glUniform1i(glGetUniformLocation(gs->program, "tex"), 0);
}

static int build_program(AVFilterContext *ctx) {
  GLuint v_shader, f_shader;
  GenericShaderContext *gs = ctx->priv;

  if (!((v_shader = build_shader(ctx, v_shader_source, GL_VERTEX_SHADER)) &&
        (f_shader = build_shader(ctx, f_shader_source, GL_FRAGMENT_SHADER)))) {
    return -1;
  }

  gs->program = glCreateProgram();
  glAttachShader(gs->program, v_shader);
  glAttachShader(gs->program, f_shader);
  glLinkProgram(gs->program);

  GLint status;
  glGetProgramiv(gs->program, GL_LINK_STATUS, &status);
  return status == GL_TRUE ? 0 : -1;
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
  vbo_setup(gs);
  tex_setup(inlink);
  return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in) {
  AVFilterContext *ctx     = inlink->dst;
  AVFilterLink    *outlink = ctx->outputs[0];

  AVFrame *out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
  if (!out) {
    av_frame_free(&in);
    return AVERROR(ENOMEM);
  }
  av_frame_copy_props(out, in);

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, inlink->w, inlink->h, 0, PIXEL_FORMAT, GL_UNSIGNED_BYTE, in->data[0]);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  glReadPixels(0, 0, outlink->w, outlink->h, PIXEL_FORMAT, GL_UNSIGNED_BYTE, (GLvoid *)out->data[0]);

  av_frame_free(&in);
  return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx) {
  GenericShaderContext *gs = ctx->priv;
  glDeleteTextures(1, &gs->frame_tex);
  glDeleteProgram(gs->program);
  glDeleteBuffers(1, &gs->pos_buf);
  glfwDestroyWindow(gs->window);
}

static int query_formats(AVFilterContext *ctx) {
  static const enum AVPixelFormat formats[] = {AV_PIX_FMT_RGB24, AV_PIX_FMT_NONE};
  return ff_set_common_formats(ctx, ff_make_format_list(formats));
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

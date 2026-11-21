#include "glview.h"

#include <SDL2/SDL.h>

#define GL_GLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <drm_fourcc.h>
#include <unistd.h>
#include <vector>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_vaapi.h>
#include <libavutil/pixfmt.h>
}
#include <va/va.h>
#include <va/va_drmcommon.h>

/////////////////////////////////////////////////////////////////////////
//// 着色器：NV12 -> RGB
//// texY  = R8  平面（Y）
//// texUV = GR88 平面（U/V 交错，每像素 2 字节）
/////////////////////////////////////////////////////////////////////////
static const char *kVertexShader = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUV;
out vec2 vUV;
void main() {
  vUV = aUV;
  gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char *kFragmentShader = R"(
#version 330 core
in vec2 vUV;
out vec4 fragColor;

uniform sampler2D texY;
uniform sampler2D texUV;
// surface 尺寸可能被驱动对齐（例如 1080 -> 1088），
// 采样时要按可见尺寸收缩 UV，否则画面底部会带进对齐的垃圾行
uniform vec2 uvScale;

void main() {
  vec2 uv = vUV * uvScale;
  float y = texture(texY, uv).r;
  vec2 cbcr = texture(texUV, uv).rg;   // (U, V)

  // BT.709 limited range (tv range) 的 YUV -> RGB
  float Y = (y - 16.0 / 255.0) * (255.0 / 219.0);
  float U = (cbcr.x - 128.0 / 255.0) * (255.0 / 224.0);
  float V = (cbcr.y - 128.0 / 255.0) * (255.0 / 224.0);

  float r = Y + 1.5748 * V;
  float g = Y - 0.1873 * U - 0.4681 * V;
  float b = Y + 1.8556 * U;
  fragColor = vec4(r, g, b, 1.0);
}
)";

static GLuint CompileShader(GLenum type, const char *src) {
  GLuint sh = glCreateShader(type);
  glShaderSource(sh, 1, &src, nullptr);
  glCompileShader(sh);
  GLint ok = GL_FALSE;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[1024] = {0};
    glGetShaderInfoLog(sh, sizeof(log) - 1, nullptr, log);
    std::fprintf(stderr, "shader compile failed: %s\n", log);
    glDeleteShader(sh);
    return 0;
  }
  return sh;
}

GLVideoView::~GLVideoView() { Close(); }

bool GLVideoView::Init(int w, int h) {
  vis_w_ = w;
  vis_h_ = h;

  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return false;
  }

  // 要 GL 3.3 core，为了用 VAO + layout(location=)
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

  SDL_Window *win = SDL_CreateWindow("120 零拷贝硬解渲染 (VAAPI + EGL dmabuf)",
                                    SDL_WINDOWPOS_CENTERED,
                                    SDL_WINDOWPOS_CENTERED, w, h,
                                    SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
  if (!win) {
    std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
    return false;
  }
  win_ = win;

  SDL_GLContext ctx = SDL_GL_CreateContext(win);
  if (!ctx) {
    std::fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
    return false;
  }
  gl_ctx_ = ctx;

  // 不限帧率，方便看真实吞吐；想要垂直同步改成 1
  SDL_GL_SetSwapInterval(0);

  std::printf("video driver = %s\n", SDL_GetCurrentVideoDriver());
  std::printf("GL_VERSION   = %s\n", (const char *)glGetString(GL_VERSION));
  std::printf("GL_RENDERER  = %s\n", (const char *)glGetString(GL_RENDERER));

  /////////////////////////////////////////////////////////////////////////
  //// SDL 在 Wayland 下用的是 EGL，所以能直接拿到它已经建好的 EGLDisplay。
  //// 如果哪天跑到 GLX 上（eglGetCurrentDisplay 返回 EGL_NO_DISPLAY），
  //// 就没法导入 dma_buf，这条路就不通了。
  /////////////////////////////////////////////////////////////////////////
  EGLDisplay dpy = eglGetCurrentDisplay();
  if (dpy == EGL_NO_DISPLAY) {
    std::fprintf(stderr,
                 "eglGetCurrentDisplay 无效：当前不是 EGL 后端（可能走了 GLX）\n");
    return false;
  }
  egl_dpy_ = dpy;

  const char *exts = eglQueryString(dpy, EGL_EXTENSIONS);
  auto has = [exts](const char *name) {
    return exts && std::strstr(exts, name) != nullptr;
  };
  if (!has("EGL_EXT_image_dma_buf_import")) {
    std::fprintf(stderr, "缺少 EGL_EXT_image_dma_buf_import\n");
    return false;
  }
  // AMD 导出的 dma_buf 带非线性 tiling modifier，没有这个扩展就导不进来
  if (!has("EGL_EXT_image_dma_buf_import_modifiers")) {
    std::fprintf(stderr, "缺少 EGL_EXT_image_dma_buf_import_modifiers\n");
    return false;
  }

  fn_create_image_ =
      (void *)eglGetProcAddress("eglCreateImageKHR");
  fn_destroy_image_ =
      (void *)eglGetProcAddress("eglDestroyImageKHR");
  fn_image_target_ =
      (void *)eglGetProcAddress("glEGLImageTargetTexture2DOES");
  if (!fn_create_image_ || !fn_destroy_image_ || !fn_image_target_) {
    std::fprintf(stderr, "EGL 扩展函数加载失败\n");
    return false;
  }

  if (!InitGL() || !InitShaders())
    return false;

  ok_ = true;
  beg_time_ = std::chrono::steady_clock::now();
  return true;
}

bool GLVideoView::InitGL() {
  // 全屏两个三角形（triangle strip），顶点位置 + 纹理坐标
  // 注意 v=0 在屏幕上方，与视频帧第一行对应
  static const float verts[] = {
      -1.f, -1.f, 0.f, 1.f, //
      1.f,  -1.f, 1.f, 1.f, //
      -1.f, 1.f,  0.f, 0.f, //
      1.f,  1.f,  1.f, 0.f, //
  };
  glGenVertexArrays(1, &vao_);
  glBindVertexArray(vao_);
  glGenBuffers(1, &vbo_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                        (void *)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                        (void *)(2 * sizeof(float)));
  glBindVertexArray(0);

  glGenTextures(1, &tex_y_);
  glBindTexture(GL_TEXTURE_2D, tex_y_);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glGenTextures(1, &tex_uv_);
  glBindTexture(GL_TEXTURE_2D, tex_uv_);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glBindTexture(GL_TEXTURE_2D, 0);
  return true;
}

bool GLVideoView::InitShaders() {
  GLuint vs = CompileShader(GL_VERTEX_SHADER, kVertexShader);
  GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kFragmentShader);
  if (!vs || !fs)
    return false;

  prog_ = glCreateProgram();
  glAttachShader(prog_, vs);
  glAttachShader(prog_, fs);
  glLinkProgram(prog_);
  glDeleteShader(vs);
  glDeleteShader(fs);

  GLint ok = GL_FALSE;
  glGetProgramiv(prog_, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[1024] = {0};
    glGetProgramInfoLog(prog_, sizeof(log) - 1, nullptr, log);
    std::fprintf(stderr, "program link failed: %s\n", log);
    return false;
  }

  uni_tex_y_ = glGetUniformLocation(prog_, "texY");
  uni_tex_uv_ = glGetUniformLocation(prog_, "texUV");
  uni_uv_scale_ = glGetUniformLocation(prog_, "uvScale");
  return true;
}

void GLVideoView::DestroyImages() {
  if (pending_y_ && fn_destroy_image_) {
    ((PFNEGLDESTROYIMAGEKHRPROC)fn_destroy_image_)((EGLDisplay)egl_dpy_,
                                                   (EGLImageKHR)pending_y_);
    pending_y_ = 0;
  }
  if (pending_uv_ && fn_destroy_image_) {
    ((PFNEGLDESTROYIMAGEKHRPROC)fn_destroy_image_)((EGLDisplay)egl_dpy_,
                                                   (EGLImageKHR)pending_uv_);
    pending_uv_ = 0;
  }
}

bool GLVideoView::DrawFrame(AVFrame *frame) {
  if (!ok_ || !frame || frame->format != AV_PIX_FMT_VAAPI)
    return false;
  if (!frame->hw_frames_ctx)
    return false;

  auto win = (SDL_Window *)win_;

  // 上一帧已经 swap 完成，可以安全销毁它引用的 EGLImage
  DestroyImages();

  ///////////////////////////////////////////////////////////////////////
  //// 1. 拿到 VADisplay 和 VASurfaceID
  ////    AV_PIX_FMT_VAAPI 帧的 data[3] 存的就是 VASurfaceID
  ///////////////////////////////////////////////////////////////////////
  AVHWFramesContext *hfc = (AVHWFramesContext *)frame->hw_frames_ctx->data;
  AVVAAPIDeviceContext *vactx =
      (AVVAAPIDeviceContext *)hfc->device_ctx->hwctx;
  VASurfaceID surface_id = (VASurfaceID)(uintptr_t)frame->data[3];

  ///////////////////////////////////////////////////////////////////////
  //// 2. surface -> dma_buf
  ////    注意：不要用 av_hwframe_map，FFmpeg 4.2 在 Mesa 上会静默退回
  ////    下载路径（返回成功但格式变成 nv12），这里直接调 libva
  ///////////////////////////////////////////////////////////////////////
  VADRMPRIMESurfaceDescriptor desc;
  std::memset(&desc, 0, sizeof(desc));
  VAStatus vst = vaExportSurfaceHandle(
      vactx->display, surface_id, VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
      VA_EXPORT_SURFACE_READ_ONLY | VA_EXPORT_SURFACE_SEPARATE_LAYERS, &desc);
  if (vst != VA_STATUS_SUCCESS) {
    std::fprintf(stderr, "vaExportSurfaceHandle failed: %d (%s)\n", vst,
                 vaErrorStr(vst));
    return false;
  }

  surf_w_ = (int)desc.width;
  surf_h_ = (int)desc.height;
  vis_w_ = frame->width;
  vis_h_ = frame->height;

  // NV12 被拆成两层：layer0 = R8(Y)，layer1 = GR88(UV)，两个 plane 常在同一个 object 里
  if (desc.num_layers < 2) {
    std::fprintf(stderr, "unexpected layers: %u\n", desc.num_layers);
    for (uint32_t i = 0; i < desc.num_objects; ++i)
      close(desc.objects[i].fd);
    return false;
  }
  int fd_y = desc.objects[desc.layers[0].object_index[0]].fd;
  int fd_uv = desc.objects[desc.layers[1].object_index[0]].fd;
  uint32_t off_y = desc.layers[0].offset[0];
  uint32_t pitch_y = desc.layers[0].pitch[0];
  uint32_t off_uv = desc.layers[1].offset[0];
  uint32_t pitch_uv = desc.layers[1].pitch[0];
  uint64_t mod_y = desc.objects[desc.layers[0].object_index[0]].drm_format_modifier;
  uint64_t mod_uv = desc.objects[desc.layers[1].object_index[0]].drm_format_modifier;

  ///////////////////////////////////////////////////////////////////////
  //// 3. dma_buf -> EGLImage
  ////    AMD 的 dma_buf 带 tiling modifier，必须把 modifier 一起传，
  ////    否则 EGL 会按线性布局理解，画面会花
  ///////////////////////////////////////////////////////////////////////
  // 8bit(NV12) 是 R8 + GR88，10bit(P010) 是 R16 + GR1616，
  // 直接使用 libva 报出来的 layer 格式，就不用手动分辨位深
  uint32_t fourcc_y = desc.layers[0].drm_format;
  uint32_t fourcc_uv = desc.layers[1].drm_format;

  auto pCreateImage = (PFNEGLCREATEIMAGEKHRPROC)fn_create_image_;
  EGLint attr_y[] = {
      EGL_WIDTH, (EGLint)surf_w_,
      EGL_HEIGHT, (EGLint)surf_h_,
      EGL_LINUX_DRM_FOURCC_EXT, (EGLint)fourcc_y,
      EGL_DMA_BUF_PLANE0_FD_EXT, fd_y,
      EGL_DMA_BUF_PLANE0_OFFSET_EXT, (EGLint)off_y,
      EGL_DMA_BUF_PLANE0_PITCH_EXT, (EGLint)pitch_y,
      EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT, (EGLint)(mod_y & 0xffffffffULL),
      EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT, (EGLint)(mod_y >> 32),
      EGL_NONE};
  EGLint attr_uv[] = {
      EGL_WIDTH, (EGLint)(surf_w_ / 2),
      EGL_HEIGHT, (EGLint)(surf_h_ / 2),
      EGL_LINUX_DRM_FOURCC_EXT, (EGLint)fourcc_uv,
      EGL_DMA_BUF_PLANE0_FD_EXT, fd_uv,
      EGL_DMA_BUF_PLANE0_OFFSET_EXT, (EGLint)off_uv,
      EGL_DMA_BUF_PLANE0_PITCH_EXT, (EGLint)pitch_uv,
      EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT, (EGLint)(mod_uv & 0xffffffffULL),
      EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT, (EGLint)(mod_uv >> 32),
      EGL_NONE};

  eglGetError(); // 清掉历史错误
  EGLImageKHR new_y = pCreateImage((EGLDisplay)egl_dpy_, EGL_NO_CONTEXT,
                                   EGL_LINUX_DMA_BUF_EXT, nullptr, attr_y);
  EGLImageKHR new_uv = pCreateImage((EGLDisplay)egl_dpy_, EGL_NO_CONTEXT,
                                    EGL_LINUX_DMA_BUF_EXT, nullptr, attr_uv);

  // EGLImage 内部已经 dup 了 fd，这里的原始 fd 由调用方负责关闭
  for (uint32_t i = 0; i < desc.num_objects; ++i)
    close(desc.objects[i].fd);

  if (!new_y || !new_uv) {
    std::fprintf(stderr, "eglCreateImageKHR failed: 0x%x\n", eglGetError());
    if (new_y)
      ((PFNEGLDESTROYIMAGEKHRPROC)fn_destroy_image_)((EGLDisplay)egl_dpy_, new_y);
    if (new_uv)
      ((PFNEGLDESTROYIMAGEKHRPROC)fn_destroy_image_)((EGLDisplay)egl_dpy_, new_uv);
    return false;
  }

  ///////////////////////////////////////////////////////////////////////
  //// 4. EGLImage -> GL 纹理（这一步就是"把显存里的帧交给 GPU"）
  ///////////////////////////////////////////////////////////////////////
  auto pImageTarget = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)fn_image_target_;
  glBindTexture(GL_TEXTURE_2D, tex_y_);
  pImageTarget(GL_TEXTURE_2D, (GLeglImageOES)new_y);
  glBindTexture(GL_TEXTURE_2D, tex_uv_);
  pImageTarget(GL_TEXTURE_2D, (GLeglImageOES)new_uv);

  // 纹理已经指向新 image，上一帧的 image 可以等到下一帧开头再销毁
  pending_y_ = img_y_;
  pending_uv_ = img_uv_;
  img_y_ = (uintptr_t)new_y;
  img_uv_ = (uintptr_t)new_uv;

  ///////////////////////////////////////////////////////////////////////
  //// 5. 绘制 + 交换缓冲
  ///////////////////////////////////////////////////////////////////////
  int out_w = 0, out_h = 0;
  SDL_GL_GetDrawableSize(win, &out_w, &out_h);
  glViewport(0, 0, out_w, out_h);
  glClearColor(0.f, 0.f, 0.f, 1.f);
  glClear(GL_COLOR_BUFFER_BIT);

  glUseProgram(prog_);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex_y_);
  glUniform1i(uni_tex_y_, 0);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, tex_uv_);
  glUniform1i(uni_tex_uv_, 1);
  glUniform2f(uni_uv_scale_, (float)vis_w_ / (float)surf_w_,
              (float)vis_h_ / (float)surf_h_);

  glBindVertexArray(vao_);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glBindVertexArray(0);

  // 调试用：GLVIEW_DUMP=/tmp/a.ppm 保存第一帧，方便和 ffmpeg 转出的图对比
  static bool dumped = false;
  if (!dumped && std::getenv("GLVIEW_DUMP")) {
    dumped = true;
    std::vector<unsigned char> px((size_t)out_w * out_h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, out_w, out_h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    if (FILE *fp = std::fopen(std::getenv("GLVIEW_DUMP"), "wb")) {
      std::fprintf(fp, "P6\n%d %d\n255\n", out_w, out_h);
      for (int y = out_h - 1; y >= 0; --y) // GL 原点在左下，翻转成图像顺序
        std::fwrite(px.data() + (size_t)y * out_w * 3, 1, (size_t)out_w * 3, fp);
      std::fclose(fp);
      std::printf("dump 第一帧到 %s\n", std::getenv("GLVIEW_DUMP"));
    }
  }

  SDL_GL_SwapWindow(win);

  Stats();
  return true;
}

void GLVideoView::Stats() {
  ++count_;
  auto now = std::chrono::steady_clock::now();
  if (now - beg_time_ >= std::chrono::seconds(1)) {
    render_fps_ = count_;
    count_ = 0;
    beg_time_ = now;
    std::printf("fps = %d\n", render_fps_);
    std::fflush(stdout);
  }
}

bool GLVideoView::IsExit() {
  if (!win_)
    return true;
  SDL_Event ev;
  while (SDL_PollEvent(&ev)) {
    if (ev.type == SDL_QUIT)
      return true;
    if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)
      return true;
  }
  return false;
}

void GLVideoView::Close() {
  DestroyImages();
  if (img_y_ && fn_destroy_image_) {
    ((PFNEGLDESTROYIMAGEKHRPROC)fn_destroy_image_)((EGLDisplay)egl_dpy_,
                                                   (EGLImageKHR)img_y_);
    img_y_ = 0;
  }
  if (img_uv_ && fn_destroy_image_) {
    ((PFNEGLDESTROYIMAGEKHRPROC)fn_destroy_image_)((EGLDisplay)egl_dpy_,
                                                   (EGLImageKHR)img_uv_);
    img_uv_ = 0;
  }
  if (prog_) {
    glDeleteProgram(prog_);
    prog_ = 0;
  }
  if (tex_y_) {
    glDeleteTextures(1, &tex_y_);
    tex_y_ = 0;
  }
  if (tex_uv_) {
    glDeleteTextures(1, &tex_uv_);
    tex_uv_ = 0;
  }
  if (vao_) {
    glDeleteVertexArrays(1, &vao_);
    vao_ = 0;
  }
  if (vbo_) {
    glDeleteBuffers(1, &vbo_);
    vbo_ = 0;
  }
  if (gl_ctx_) {
    SDL_GL_DeleteContext((SDL_GLContext)gl_ctx_);
    gl_ctx_ = nullptr;
  }
  if (win_) {
    SDL_DestroyWindow((SDL_Window *)win_);
    win_ = nullptr;
  }
  ok_ = false;
}

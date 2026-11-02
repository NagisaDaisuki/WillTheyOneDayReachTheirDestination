#ifndef XVIDEO_VIEW_H
#define XVIDEO_VIEW_H
#include <mutex>

//////////////////////////////////
//// 视频渲染接口类
//// 隐藏 SDL/OpenGL 实现
//// 渲染方案可替代
//// 线程安全
//////////////////////////////////

// 前向声明
struct AVFrame;

class XVideoView {
public:
  enum PixFormat { RGBA = 0, ARGB, YUV420P };
  enum RenderType { SDL = 0, OPENGL, DIRECTX };

  // 静态成员函数创建类对象
  static XVideoView *Create(RenderType type = SDL);

  // 基类虚析构：保证通过基类指针 delete 时正确释放派生类资源
  virtual ~XVideoView() = default;

  /////////////////////////////////////////
  //// 初始化渲染窗口 线程安全
  //// @para w 窗口宽度
  //// @para h 窗口高度
  //// @para fmt 像素格式
  //// @para win_id 窗口句柄, 如果为空，创建新窗口
  ////   - SDL 后端: X11 Window 句柄（如 QWidget::winId()）
  ////   - OPENGL 后端: 宿主 QWidget 指针（嵌入为子控件）
  //// @return 是否创建成功
  /////////////////////////////////////////
  virtual bool Init(int w, int h, PixFormat fmt = RGBA,
                    void *win_id = nullptr) = 0;

  /////////////////////////////////////////
  //// 渲染图像 线程安全
  //// @para data 渲染的二进制图像数据
  //// @para linesize 图像每行字节数, 对于 YUV420P就是Y一行字节数
  //// linesize <= 0 就根据宽度和像素格式自动算出大小
  //// @return 是否渲染成功
  /////////////////////////////////////////
  virtual bool Draw(const unsigned char *data, int linesize = 0) = 0;

  /////////////////////////////////////////
  //// 三平面渲染重载 线程安全
  //// 用于 FFmpeg AVFrame 等平面分离存储的 YUV 数据
  //// （data/u/v 指针独立、每平面有各自的 linesize 对齐）
  //// @return 是否渲染成功
  /////////////////////////////////////////
  virtual bool Draw(const unsigned char *y, int y_pitch, const unsigned char *u,
                    int u_pitch, const unsigned char *v, int v_pitch) = 0;

  /////////////////////////////////////////
  //// 清理所有申请的资源，包括关闭窗口
  /////////////////////////////////////////
  virtual void Close() = 0;

  /////////////////////////////////////////
  //// 处理窗口退出事件
  /////////////////////////////////////////
  virtual bool IsExit() = 0;

  /////////////////////////////////////////
  //// 窗口显示缩放参数
  //// @para w 传入实际窗口渲染 宽度
  //// @para h 传入实际窗口渲染 高度
  /////////////////////////////////////////
  void Scale(int w, int h);

  /////////////////////////////////////////
  //// 根据 AVFrame 判断 渲染方式
  /////////////////////////////////////////
  bool DrawFrame(AVFrame *frame);

protected:               // 在派生类中访问
  int width_ = 0;        // 窗口宽度
  int height_ = 0;       // 窗口高度
  PixFormat fmt_ = RGBA; // 像素格式
  std::mutex mtx_;       // 线程安全互斥锁

  // 实际显示窗口大小
  int scale_w_ = 0;
  int scale_h_ = 0;
};

#endif

// ============================================================================
//  xencode.h —— 崩溃修复快照（本文件与修复前完全一致，仅补充 [KNOWN-ISSUE] 注释）
//
//  注意：导致 SIGSEGV 的 bug 不在封装层里，而在调用方
//        （116_test_xencode.cpp 的 UV 填充循环，见 [BUGFIX-1]）。
//        本文件里列出的是封装层本身的遗留隐患。
// ============================================================================
#pragma once

#include <mutex>
#include <vector>

// forward definition
struct AVFrame;
struct AVPacket;
struct AVCodec;
struct AVCodecContext;

class XEncode {
public:
  ~XEncode(); // 增加析构函数

  ///////////////////////////////////////////////
  /// 创建编码上下文
  /// 与成员无关 static
  /// @para codec_id 编码器 ID号，对应 FFmpeg
  /// @return 编码上下文，失败返回 nullptr
  ///////////////////////////////////////////////
  // [KNOWN-ISSUE-6] 接口类型安全：
  //   因为头文件里只有 struct AVCodecContext; 这样的前向声明，
  //   AVCodecID 这个 enum 类型传不进来，只能退化成 int，调用方写错了编译器不报错。
  //   且返回裸指针 + 需要调用者自己交给 set_c()，中间任何一步失败都会泄漏：
  //       auto c = XEncode::Create(id);   // 若此处抛异常/提前 return → 泄漏
  //       en.set_c(c);
  static AVCodecContext *Create(int codec_id);

  ///////////////////////////////////////////////
  /// 设置对象的编码器上下文，上下文传递到对象中，
  /// 空间由 XEncode维护
  /// @para c 编码器上下文，如果 c_ 不为 nullptr，
  /// 则先清理资源
  ///////////////////////////////////////////////
  // [KNOWN-ISSUE-7] 裸指针所有权转移，不满足 RAII：
  //   1) 两个 XEncode 对象可以 set_c() 同一个指针 → double free；
  //   2) 对象没有拷贝控制（mutex 隐式禁掉拷贝，但 move 也没了），
  //      传参/返回都只能用引用；
  //   3) 没有任何机制阻止“已 Open 的上下文”被换掉。
  void set_c(AVCodecContext *c);

  ///////////////////////////////////////////////
  /// 设置编码参数，线程安全
  /// C++ 支持函数重载
  /// @return 是否正确添加参数
  ///////////////////////////////////////////////
  bool SetOpt(const char *key, const char *value);
  bool SetOpt(const char *key, int value);

  ///////////////////////////////////////////////
  /// 打开编码器 线程安全
  ///////////////////////////////////////////////
  bool Open();

  ///////////////////////////////////////////////
  /// 编码数据 线程安全 每次新创建AVPacket
  /// @para frame 空间由用户维护
  /// @return 失败返回一个空 vector
  /// 返回的 std::vector<AVPacket*>
  /// av_packet_free() 清理
  ///////////////////////////////////////////////
  std::vector<AVPacket *> Encode(const AVFrame *frame);

  ///////////////////////////////////////////////
  /// 创建一个 AVFrame，根据 AVCodecContext，需要
  /// 调用者释放
  ///////////////////////////////////////////////
  AVFrame *CreateFrame();

  ///////////////////////////////////////////////
  /// 返回所有编码缓冲中的 AVPacket
  ///////////////////////////////////////////////
  std::vector<AVPacket *> End();

private:
  // [KNOWN-ISSUE-1] 析构与 set_c() 不同步：~XEncode() 没有加锁，
  //   若与 set_c()/Encode() 并发，存在 use-after-free（虽然“析构时别人还在用”
  //   本身已是 UB，但接口承诺了“线程安全”就应该一致）。
  AVCodecContext *c_ = nullptr;
  std::mutex mtx_;
};

#ifndef RGBYUVPLAYER_H
#define RGBYUVPLAYER_H

#include "xvideoview.h"
#include <QWidget>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

class QLabel;

QT_BEGIN_NAMESPACE
namespace Ui {
class RgbYuvPlayer;
}
QT_END_NAMESPACE

class RgbYuvPlayer : public QWidget {
  Q_OBJECT

public:
  explicit RgbYuvPlayer(QWidget *parent = nullptr);
  ~RgbYuvPlayer() override;
  void resizeEvent(QResizeEvent *event) override;
  void moveEvent(QMoveEvent *event) override;
  void initSDL();
  void ReverseRow(unsigned char *row, size_t n);
  void Main(); // 线程入口函数

signals:
  void ViewS(); // 信号函数，将任务放在列表
public slots:
  void View();      // 槽函数，接收信号函数信号执行
  void Open(int i); // 通过编号进行设定指定窗口播放
  void Open1();
  void Open2();

protected:
  // 监听 label 的 resize，把真实大小同步给渲染器
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  // 把两个 label 的当前大小同步给对应的渲染器
  void SyncScale();

  // 把 fps 文本贴到 video label 的左上角（做成原生子窗口，不会被 SDL 画面盖住）
  void AttachFpsOverlay(QLabel *fps, QWidget *video);

  // 校验裸视频文件大小与 宽×高×像素格式 是否对得上，不对就告警
  void CheckFileSize(int i, int w, int h, XVideoView::PixFormat fmt);

  Ui::RgbYuvPlayer *ui;
  // 播放状态全部成员化
  std::vector<XVideoView *> views_;
  std::vector<std::chrono::steady_clock::time_point> last_pts_;
  std::thread th_;
  std::atomic<bool> is_exit_{false}; // 跨线程访问，必须原子
};
#endif // RGBYUVPLAYER_H

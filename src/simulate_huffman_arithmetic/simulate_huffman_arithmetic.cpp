#include <iomanip>
#include <iostream>
#include <map>
#include <string>

// ==========================================
// 1. 变长编码模拟 (类似哈夫曼)
// ==========================================

void simulateHuffman(const std::string &input) {
  std::cout << "========== 变长编码 (VLC/哈夫曼) ==========\n";

  // 假设我们已经根据 8:2 的概率建好了哈夫曼树
  // 显然，A 出现的多，给短码 "0"；B 出现的少，给长码 "10"
  std::map<char, std::string> codeMap = {
      {'A', "0"},
      {'B', "10"},
  };

  std::string encodedBits = "";
  std::cout << "编码过程：\n";
  for (char c : input) {
    std::cout << c << " -> " << codeMap[c] << "\n";
    encodedBits += codeMap[c];
  }
  std::cout << "最终比特流：" << encodedBits << "\n";
  std::cout << "总消耗比特数：" << encodedBits.length() << " bits\n\n";
}

// ==========================================
// 2. 算术编码模拟 (分地皮)
// ==========================================
void simulateArithmetic(const std::string &input) {
  std::cout << "========== 算术编码 (Arithmetic Coding) ==========\n";

  // 初始区间 [0,1)
  double low = 0.0;
  double high = 1.0;

  // 设定概率分布：A占 [0.0, 0.8) B 占 [0.8, 1.0)
  double probA_low = 0.0, probA_high = 0.8;
  double probB_low = 0.8, probB_high = 1.0;

  std::cout << std::fixed << std::setprecision(4); // 保留 4 位 小数方便查看
  std::cout << "开始区间：[" << low << ", " << high << ")\n";

  for (char c : input) {
    double range = high - low; // 当前区间的跨度

    if (c == 'A') {
      // 把当前区间切出 80% 的起点作为新的high
      high = low + range * probA_high;
      // low 保持不变 (A 的起点是 0)
    } else if (c == 'B') {
      // 把当前区间切出 80% 的位置作为新的high
      low = low + range * probB_low;
      // high 保持不变（因为B的终点是1）
    }
    std::cout << "输入: '" << c << "' 后，当前区间缩小为：[" << low << ", "
              << high << ")\n";
  }

  // 挑一个中间的数发出去
  double finalValue = (low + high) / 2.0;
  std::cout << "最终区间极小，我们只需传输一个数字，比如中点:" << finalValue
            << "\n";

  // 模拟解码
  std::cout << "\n开始解码 (用收到的 " << finalValue << " 反推): \n";
  double decodeLow = 0.0, decodeHigh = 1.0;
  double currentValue = finalValue;

  for (size_t i = 0; i < input.length(); ++i) {
    double range = decodeHigh - decodeLow;
    // 计算当前值 在区间里的相对位置
    double scaledValue = (currentValue - decodeLow) / range;

    if (scaledValue < 0.8) {
      std::cout << "解出: A\n";
      decodeHigh = decodeLow + range * probA_high;
    } else {
      std::cout << "解出: B\n";
      decodeLow = decodeLow + range * probB_low;
    }
  }
  std::cout << "解码完成，完美还原！\n";
}

int main(int argc, char *argv[]) {
  std::string data = "AABABAA";
  std::cout << "原始数据：" << data << "\n\n";

  simulateHuffman(data);
  simulateArithmetic(data);

  getchar();
  return 0;
}

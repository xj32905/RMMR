# RMMR - RM 视觉组培训仓库

## 目录结构
- W1/：第1周学习记录
- W2/：第2周学习记录
- W3/：第3周学习记录
- project/armor_detector/：装甲板识别项目代码

## 学习记录
### W1
记录本周遇到的问题和解决过程：# W1 学习记录

## 学习进度
- 完成了 Linux 开发环境配置，验证了 CMake、g++、gcc、Git、Make 工具的安装与版本。
- 编写并运行了第一个 C++ 程序 `hello.cpp`，熟悉了 `g++` 编译命令与终端运行流程。
- 复习了 C++ 函数参数传递的三种方式：值传递、引用传递、指针传递，并通过 `swap_demo.cpp` 验证了效果。
- 初步了解了 CMake 构建工具的基本使用方法。
.模板让你写一套代码，能自动适应多种数据类型。**
- 关键词：`template <typename T>`
- `T` 是**类型占位符**，可以是 `int`、`double`、`string`、自定义类等
- 编译器会根据你调用时传入的具体类型，自动生成对应版本（**编译时多态**）

## 不熟悉的概念
- 指针与引用的底层实现差异
- CMake 多文件项目的配置方式
- 命令行参数的解析与处理

## 本周问题与解决过程
### 问题：值传递无法交换变量值
**现象**：在 `swap_demo.cpp` 中，值传递版本的 `swap` 函数执行后，原变量的值没有变化。
**原因**：值传递是将变量的副本传入函数，函数内修改的只是副本，不会影响原变量。
**解决**：改用引用传递或指针传递，让函数直接操作原变量的地址，成功实现了变量交换。

**问题：** swap_demo.cpp 编译失败，报 `template` 相关语法错误。  
**原因：** 在 `main` 函数前错误地写了 `template<swapNum>`，这不是合法的模板声明。  
**解决过程：** 查了 C++ 模板语法，理解到 `template` 必须配合 `typename` 或 `class` 关键字，且需要紧跟在函数或类定义前。将交换逻辑改写为普通函数，再逐步封装为类、模板类，最终编译通过并扩展了功能。

问题描述
在提交代码时，不小心把 g++ hello.cpp -o hello 生成的可执行文件 hello 也一并提交到了远程仓库。

组长反馈
“build 不用提交，不要进 git”

问题原因

    对 Git 追踪规则不够熟悉，没有意识到编译出来的二进制文件属于“构建产物”，不应纳入版本控制。

    仓库没有配置 .gitignore，导致 git add . 时误把可执行文件也添加了进去。

解决过程

    用 git rm hello 将文件从 Git 仓库中删除（同时删除本地该文件）。

    在仓库根目录创建 .gitignore 文件，并写入 hello，让 Git 永久忽略该文件。

    执行 git add .gitignore 和 git commit 提交忽略规则。

    使用 git pull --rebase 同步远程更新，再通过 git push 推送到远程。

    确认远程仓库中 hello 文件已消失，后续再编译也不会被追踪。

总结收获

    清楚了“构建产物”的定义：由源代码编译生成的文件（如可执行文件、.o、.exe 等），不应提交到仓库。

    掌握了 .gitignore 的基本用法，能阻止特定文件或文件夹被 Git 追踪。

    理解了 git rm 与 rm 的区别，学会了如何彻底移除已追踪的文件。

    学习了冲突时的 git pull --rebase 处理方式。

# ROS2 作业提交 - training_pkg

## 环境与基础概念
- ROS2 Humble 已安装，turtlesim 运行正常
- workspace: `/home/xj/town_ws`
- package: `training_pkg`
- node: `talker`（发布者）、`listener`（订阅者）
- topic: `/chatter`（String 类型）

## 基础通信
- C++ 节点 talker/listener 已实现
- 编译命令：`colcon build --packages-select training_pkg`
- 运行：开两个终端分别执行 `ros2 run training_pkg talker` 和 `ros2 run training_pkg listener`

## launch 与参数
- launch 文件：`launch/talker_listener.launch.py`
- 参数通过 YAML 文件加载：`config/my_params.yaml`
- 参数 `my_param` 在 talker 启动时打印，证明生效
- 运行 launch：`ros2 launch training_pkg talker_listener.launch.py params_file:=/home/xj/town_ws/src/training_pkg/config/my_params.yaml`

## bag 调试
- 录制：`ros2 bag record /chatter`
- 查看信息：`ros2 bag info rosbag2_*`
- 回放：`ros2 bag play rosbag2_*`（同时运行 listener 接收历史消息）

## 进阶完成
- [x] 参数 YAML 文件加载
- [ ] service / 自定义消息（未做，可选）

## 运行方式总结
```bash
cd ~/town_ws
colcon build --packages-select training_pkg
source install/setup.bash
ros2 launch training_pkg talker_listener.launch.py params_file:=~/town_ws/src/training_pkg/config/my_params.yaml
遇到的问题及解决

    编译时找不到 talker.cpp → 修正文件名

    CMakeLists.txt 拼写错误 → 修正 ament_cmake 等

    launch 找不到文件 → 添加 install(DIRECTORY launch) 到 CMakeLists

    gedit 崩溃 → 改用 nano
# RM Traditional Vision Armor Detection

## 项目简介

本项目实现了一个基于 OpenCV 的传统视觉装甲板识别系统，适用于 RoboMaster（RM）视觉任务中的：

* 红蓝灯条检测
* 装甲板配对
* 视频与图片识别
* 高亮环境下的过曝灯条处理

系统整体采用：

```text
图像增强
→ 高亮区域提取
→ 灯条筛选
→ 红蓝分类
→ 几何配对
→ 装甲板输出
```

的经典传统视觉流程。

---

# 项目特点

## 1. Gamma 高亮压制

通过 Gamma Correction：

对高亮区域进行非线性压缩。

主要作用：

* 抑制 LED 过曝
* 恢复亮部细节
* 提高强光环境稳定性

---

## 2. HSV 颜色空间处理

系统使用 HSV 色彩空间：

* H：色相
* S：饱和度
* V：亮度

相比 BGR：

HSV 更适合：

* 高亮区域提取
* 红蓝灯条分类
* 强光环境分析

---

## 3. 高亮区域提取

系统基于 V 通道：

```cpp
threshold(V, thresh, 255)
```

提取高亮区域。

当前版本主要采用：

* 亮度阈值
* 闭运算
* 开运算

得到可能的灯条候选区域。

---

## 4. 形态学优化

使用：

* MORPH_CLOSE
* MORPH_OPEN

完成：

* 灯条连接
* 小噪点消除
* 区域修补

提高轮廓稳定性。

---

## 5. 几何灯条筛选

系统通过：

* 面积
* 长宽比
* 长边角度

筛选符合灯条特征的候选区域。

灯条需要满足：

* 细长
* 接近竖直
* 面积合理

---

## 6. 红蓝灯条分类

系统通过 HSV ROI 分析：

```cpp
countNonZero(red_mask)
countNonZero(blue_mask)
```

统计：

* 红色像素数量
* 蓝色像素数量

从而完成灯条颜色分类。

---

## 7. 装甲板几何配对

系统通过：

* 长度相近
* 平行性
* 中心距离
* Y轴偏差

完成左右灯条配对。

最终得到：

```text
[left light bar] + [right light bar]
→ armor
```

---

# 系统整体流程

```text
输入图像
↓
Gamma压亮
↓
HSV转换
↓
提取高亮区域
↓
形态学处理
↓
轮廓提取
↓
灯条几何筛选
↓
红蓝分类
↓
灯条配对
↓
输出装甲板
```

---

# 文件结构

```text
main.cpp
│
├── 图像增强
│   ├── gammaCorrect()
│   ├── equalizeHist()
│   └── mask()
│
├── 颜色空间处理
│   ├── conventColor()
│   ├── split_red()
│   └── split_blue()
│
├── 高亮区域提取
│   └── split_overexpose()
│
├── 轮廓处理
│   ├── contour()
│   ├── draw_cont_tra()
│   └── trans_contour2rotatedRect()
│
├── 装甲板检测
│   └── geo_armorDetect()
│
└── 绘制模块
    ├── draw_rect()
    └── draw_box()
```

---

# 编译方式

## Ubuntu + OpenCV4

```bash
g++ main.cpp -o detect `pkg-config --cflags --libs opencv4`
```

---

# 使用方法

## 图片模式

```bash
./detect -p image.jpg
```

---

## 视频模式

```bash
./detect -v input.mp4
```

输出：

```text
output.mp4
```

---

# 当前存在的问题

## 1. 高亮环境依赖固定阈值

当前：

```cpp
threshold(V, 220)
```

属于固定阈值。

在：

* 3000lux
* 5000lux

环境下：

背景可能整体超过阈值。

导致：

```text
mask全白
→ contour爆炸
```

---

## 2. HSV 在过曝环境下不稳定

高亮环境下：

```text
Saturation ↓
```

导致：

* 红蓝分类不稳定
* 灯条发白
* HSV失效

---

## 3. 缺少动态曝光适配

当前系统：

未根据环境亮度：

* 动态调整阈值
* 动态压制高亮

导致：

强光环境鲁棒性不足。

---

# 后续优化方向

## 1. 动态阈值

替换：

```cpp
threshold(V, 220)
```

为：

```cpp
threshold(V, avg + offset)
```

提高环境适应能力。

---

## 2. BGR 差分替代 HSV

使用：

```text
R - (B+G)/2
B - (R+G)/2
```

提高强光环境稳定性。

---

## 3. CLAHE 图像增强

使用：

```text
CLAHE
```

恢复局部过曝细节。

---

## 4. 灯条核心提取

提取：

```text
超高亮核心区域
```

提高灯条定位稳定性。

---

## 5. 时序跟踪

加入：

* 卡尔曼滤波
* ROI追踪
* 历史装甲板缓存

提高视频连续稳定性。

---

# 项目阶段评估

当前版本属于：

```text
传统视觉中级版本
```

已经具备：

* 基础鲁棒性
* 结构化流程
* 较完整的视觉框架

适合作为：

* RM传统视觉学习
* OpenCV装甲板检测入门
* 后续神经网络视觉前端

的基础工程。

---

# 总结

本项目核心思想：

```text
通过图像增强与几何约束，
从复杂环境中逐步提取装甲板特征。
```

传统视觉的本质不是“识别”，

而是：

```text
不断净化图像，
直到目标从背景中被分离出来。
```

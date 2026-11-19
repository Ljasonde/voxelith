# Voxelith 性能实测

2026-11-19，Windows 11、Ryzen 7 8845H、RTX 4060 Laptop（8188 MiB，621.01）、MSVC 19.41 Release。输出均为 1920×1080。缩放 0.7 对应 1344×756 的内部几何和光照缓冲，后处理输出原分辨率。环境未锁定 GPU 频率或功耗。

## 四机位相机路径

固定种子 241128，暖机 120 帧，记录 600 帧。每 150 帧切换遗迹或出生岛，先静止 35 帧，再沿弧线移动。世界含 167 个非空网格区块，共 544,540 个三角形。基准在加载和加速结构初建完成后开始。窗口组包含交换链获取、提交、呈现调用和 GPU fence 等待，VSync 关闭。离屏组省去交换链，使用同一套着色器。两组均未绘制交互 HUD。固定路线覆盖渲染，玩家编辑和重新打包产生的卡顿需要另外分析。

平均 FPS 为 600 除以总耗时。P1、P99 为逐帧总耗时按升序排列后的最近秩百分位。CPU 列记录录制命令耗时，GPU 由 timestampPeriod 换算；等待、传输、提交与呈现会影响总耗时。CPU/GPU 有重叠，列值不能相加。窗口 FPS 表示应用提交并完成的帧数；屏幕上实际可见的刷新次数受显示器限制。

| 场景 | 比例 | 视距 | 平均 FPS | 帧时 P1 / P99（ms） | GPU 均值（ms） |
|---|---:|---:|---:|---:|---:|
| [窗口 · RT](benchmarks/v1/window/rt/summary.json) | 1.0 | 12 | 911.0 | 0.81 / 1.80 | 0.717 |
| [窗口 · 光栅](benchmarks/v1/window/raster/summary.json) | 1.0 | 12 | 1267.8 | 0.51 / 1.29 | 0.480 |
| [窗口 · RT（0.7）](benchmarks/v1/window/rt-07/summary.json) | 0.7 | 12 | 1178.0 | 0.60 / 1.39 | 0.510 |
| [离屏 · RT](benchmarks/v1/offscreen/rt/summary.json) | 1.0 | 12 | 998.2 | 0.73 / 2.10 | 0.786 |
| [离屏 · 光栅](benchmarks/v1/offscreen/raster/summary.json) | 1.0 | 12 | 1405.9 | 0.43 / 1.84 | 0.531 |
| [离屏 · RT 直接绘制](benchmarks/v1/offscreen/rt-direct/summary.json) | 1.0 | 12 | 984.6 | 0.78 / 2.07 | 0.818 |
| [离屏 · RT（0.7）](benchmarks/v1/offscreen/rt-07/summary.json) | 0.7 | 12 | 1331.3 | 0.53 / 1.84 | 0.559 |
| [离屏 · RT（视距 8）](benchmarks/v1/offscreen/rt-8/summary.json) | 1.0 | 8 | 1039.4 | 0.74 / 1.95 | 0.746 |
| [离屏 · 光栅直接绘制](benchmarks/v1/offscreen/raster-direct/summary.json) | 1.0 | 12 | 1365.6 | 0.53 / 1.88 | 0.548 |
| [离屏 · RT 仅视锥](benchmarks/v1/offscreen/rt-no-history/summary.json) | 1.0 | 12 | 1024.8 | 0.72 / 2.05 | 0.770 |

## 同一窗口 RT 运行的 GPU 分区

| 区段 | 平均 ms | 命令范围 |
|---|---:|---|
| 剔除 | 0.008 | 清空计数、视锥及可用的历史深度测试、间接屏障 |
| 光栅阴影 | 0.000 | RT 模式跳过 CSM |
| G-buffer 与深度历史 | 0.159 | 几何填充；历史有效时生成 Hi-Z |
| TLAS | 0.066 | 每帧构建实例加速结构；光栅分支在此计算 SSAO |
| 光照与 ray query | 0.350 | 不透明面光照及查询，随后执行前向水面折射与反射 |
| 后处理 | 0.133 | 六层 Bloom 下采样、合成、ACES 和 FXAA |

## 优化记录

1. 初版单机位前向渲染分别保存在 [逐区块](benchmarks/forward-direct/frames.csv) 和 [GPU 间接](benchmarks/forward-indirect/frames.csv)。后续改为四机位、延迟光照和更新的遗迹几何，两次实验各自比较。
2. 四机位光栅版逐帧重绘三张阴影图，[基线](benchmarks/raster-native/frames.csv)的阴影均值为 7.112 ms。缓存每个级联的光源矩阵和深度图，并在编辑、缩放或相机超出容差时失效，[复测](benchmarks/final-raster/frames.csv)为 0.479 ms。此轮同时减少移动相机上的 Hi-Z 构建，分区可区分两处变化。整帧平均 8.523 → 2.886 ms。
3. 改用 staging 批量复制到设备优先的几何 arena，并将像素噪声改为纹理数组采样。RT 组 G-buffer/Hi-Z 均值由 [1.874 ms](benchmarks/final-rt/frames.csv) 降到 [0.162 ms](benchmarks/release/rt/frames.csv)。两项一起提交，无法由这组数据分开归因。内存分配通过 VMA 完成，运行中未采集 PCIe 传输计数器。
4. 最终离屏 RT 路线在直接绘制时每帧平均提交 163.1 个地形 draw。间接路径提交一次地形 draw 调用，平均保留 93.9 个区块。仅开视锥时为 96.4 个。此场景历史深度减少的工作量有限，构建金字塔的成本可抵消收益；保留独立的 Hi-Z 开关。

## 验证与复现

`tools/benchmark.ps1` 运行七组离屏对照。窗口运行示例为 `out/Release/Voxelith.exe --present --benchmark benchmarks/window --width 1920 --height 1080 --rt 1`。每个目录都有 frames.csv 与 summary.json。窗口 RT 样本中 600 帧最长 2.308 ms，P99 为 1.795 ms。编辑突然改变大量网格、后台应用争抢资源或长时间热降频均超出这次测量的覆盖范围。

1600×900 静止相机第六帧，间接绘制含 Hi-Z 与直接绘制的 144 万像素 RGBA 最大差值为 0。CPU 测试六项分别验证网格确定性、DDA 距离、AABB 与跳跃、噪声范围、三核心传送门、存档往返。验证层检查覆盖窗口和离屏两条路径；SDK 的 vulkaninfo 枚举系统显示扩展时打印的混合显卡驱动提示单独记录在 DEVLOG。

2026-11-19 交付版原始数据在 `benchmarks/v1/`。历史对照保留于同级目录。水面作为额外的一次前向 draw 计入光照时间；上文 draw 次数指不透明地形。窗口交互开启同步验证连续呈现 180 帧，离屏两条渲染路径分别运行 720 帧，均为 0 warning、0 error。

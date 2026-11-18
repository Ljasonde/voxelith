# 开发记录

## 2026-11-18 · M0

RTX 4060 Laptop 8188 MiB，驱动 621.01，Vulkan SDK 1.4.363.0，MSVC 19.41。1600×900 GLFW 窗口呈现 12 帧，另起进程完成离屏 PNG，分别为 0 warning、0 error。截图见 docs/media/m0.png。CMake 在中文源目录下探测编译器时出现 0xC0000409，一键脚本临时映射空闲盘符，结束后解除。下一步为实体区块、相机和地形。

## M1 · 区块与漫游镜头

生成 416×128×416 的有限群岛，32³ 区块网格按材质与顶点 AO 合并。固定航线实机截图 docs/media/m1.png，validation 0/0。下一步为玩家状态与存档。
M2：接入玩家、DDA、三核心、传送门、存档和菜单。六项 CPU 测试全部通过，第二遗迹截图为 docs/media/m2.png。原始前向基线位于 docs/benchmarks/forward-*。
M3/M5：延迟 G-buffer、三层 2048 阴影、半分辨率 SSAO、HDR/Bloom/FXAA；每个非空不透明区块构建 BLAS，双缓冲 TLAS 每帧构建。两条路径各完成一张验证层截图，0 warning / 0 error。Hi-Z 前的 RT 基线见 docs/benchmarks/pre-hiz。

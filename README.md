# 简易焊台

 Fork from https://github.com/zeromake/simple_solder  
来自 https://oshwhub.com/lilinkai/jian-dan-jia-re-tai

## 功能特性 (更新)

经过重构优化，提升了交互体验并加入新的功能特性：

- **无阻塞主循环**：采用后台 PWM 模拟、数码管动态扫描和高频的旋转编码器读取。
- **动态旋转步进**：低转速时EC11步进降低为1以精确调整温度，高转速时恢复为5。
- **数码管显示效果**：
  - 在调节或设定温度时，数码管的小数点会以“跑马灯”的形式闪烁，以区分当前显示值是“设定温度”还是“实际温度”。
- **多挡位预设 (EC11 按键)**：
  - 默认提供四个温度挡位：0°C、150°C、200°C、250°C。
  - **短按 EC11 按键**：在四个挡位之间循环切换。切换时会短暂显示目标温度。
  - **长按 EC11 按键 (~1秒)**：保存当前旋钮调节的温度，覆盖当前挡位的默认温度设置，写入 EEPROM 。
  - **挡位指示**：在显示实际温度时，小数点指示当前处于的挡位（1档无小数点，2档百位亮，3档百位十位亮，4档全亮）。

#### **使用模型文件请务必检测螺丝孔位，当前作者与原作者使用的加热板螺丝孔位不一致，略有修改，切勿直接打印**

## 编译方法

下载 sdcc 并设置到 path
``` sh
> xrepo add-repo zeromake https://github.com/zeromake/xrepo.git
> xrepo install sdcc
> xrepo info sdcc
installdir: C:\Users\ljh\AppData\Local\.xmake\packages\s\sdcc\4.5.0\e6aa3c720cd8435ba7f7806fe30454fe
# 把对应的 installdir/bin 添加到 PATH 里
# pwsh
> $env:PATH+=";$installdir\bin"
# bash
> export PATH = "$PATH:$installdir/bin"
```

```sh
# xmake f 会自动下载 fwlib_stc8 依赖并编译
xmake f -p cross -a mcs51 --toolchain=sdcc -c
xmake b root
# 编译输出在 build/cross/mcs51/release/boot.bin
```

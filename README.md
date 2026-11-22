# LixinOS 1.0.0

一个从零编写的 AArch64 裸机 C 内核。第一代由宿主机交叉编译。启动后，内核可以编辑自己的 C 源码，用客机内的 TinyCC 编译出下一代 ELF，写入磁盘，再把 CPU 控制权交给新内核。第二代可重复整个过程。

## 运行

需要 QEMU 的 `qemu-system-aarch64`、Python 3 和 `shasum`。发布目录含预编译启动镜像，启动和客机自举均可离线完成。

```sh
./run.sh
```

启动器默认使用 `build/system.img`，首次使用时创建 1 MiB 的空磁盘。也可以传入一个新的路径，例如 `./run.sh build/experiment.img`。在客机提示符下输入下列命令。

```text
version
note my-session
replace hello-kernel|hello-from-tcc
build
switch
note
version
```

新盘从 `build=0 slot=A hot=0` 开始。第一次切换后应显示 `build=1 slot=B hot=1 message=hello-from-tcc`，查询备注应返回 `note=my-session`。再执行一次 `build`、`switch` 即可进入第二个客机编译的内核。输入 `rollback` 会切到另一个有效槽并把它设为下次启动的版本。`off` 关闭虚拟机，再次运行启动器可验证冷启动结果。

`source` 显示当前完整内核源文件。`replace OLD|NEW` 替换其中第一次出现的字面文本；宿主端也可以编辑 `src/` 后重新构建种子。客机里的源码修改在成功执行 `switch` 后与 ELF 一起落盘。`build` 只在内存中准备候选 ELF，编译失败会清除候选状态。

## 自举和切换

- **种子和工具。** `build.sh` 用 Arm GNU 工具链构建种子内核、常驻 ELF 加载器和上游 TinyCC 0.9.28rc 的 AArch64 编译库。构建脚本固定 TinyCC 提交 `f830a1697e159056b75de40a60464bc43ee670a3`。工具库在客机 CPU 上完成预处理、编译和链接，输出写入客机内存。这版自举的对象是 `src/abi.h` 和 `src/kernel.c` 组成的完整内核。常驻加载器、TinyCC 和其 C 运行库由启动镜像提供。
- **内核职责。** 可替换的内核实现 PL011 串口、VirtIO 块设备驱动、双槽安装与回滚、源码编辑和命令循环。加载器校验 AArch64 ELF 和段的内存边界，装入段、清零 BSS、重置栈并同步指令缓存。新内核重新初始化设备。
- **磁盘提交。** 内核将 ELF 和源码写到备用槽，刷盘并读回核验，再写入带代数和校验值的槽头并刷盘。冷启动按提交代数选择有效槽。最近的副本校验失败时，尝试另一槽；两槽均不可用时，启动镜像仍提供种子内核。校验覆盖意外损坏。格式正确但运行中卡死的内核需要另选工作盘恢复。
- **在线状态。** 交接结构保留备注和本次开机的切换次数。冷启动会重置这两项。这个版本是单核、单地址空间的串口系统，执行模式是内核独占 CPU，没有用户进程、线程或网络连接迁移。

## 从源码构建与验证

```sh
./build.sh
python3 test.py
```

宿主构建需要 Git、Python 3、C 编译器和带 newlib 的 `aarch64-none-elf-gcc`。Apple Silicon 上可以自动下载并校验 Arm GNU 14.3.Rel1。其他平台在 PATH 中提供同目标的工具链。`build/boot.bin` 存在时，启动器优先使用源码构建产物；干净检出使用经 SHA-256 校验的 `release/boot.bin`。

测试在临时磁盘上运行真实 QEMU，覆盖两代客机编译、源码修改生效、同一虚拟机内交接、失败构建隔离、冷启动、回滚持久化和损坏槽回退。关机后，测试还从镜像独立读取两代产物，检查 AArch64 ELF 标识及配套源码。

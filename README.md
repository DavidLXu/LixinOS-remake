# LixinOS 1.2.0

一个从零编写的 AArch64 裸机 C 内核。第一代由宿主机交叉编译。启动后，内核可以编辑自己的 C 源码，用客机内的 TinyCC 编译出下一代 ELF，写入磁盘，再把 CPU 控制权交给新内核。第二代可重复整个过程。

## 运行

Linux 或 macOS 上准备 QEMU 的 `qemu-system-aarch64`、POSIX Shell 及 `sha256sum` 或 `shasum` 即可启动。发布目录含预编译启动镜像。手动编译、切换、回滚和验收都可以离线完成。

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

## 客机 TCC 用户态命令

在新盘上执行下列命令。首次使用工具时客机自动把 C 源码编译成独立的 AArch64 ELF。`tcc tools` 可显式重新编译。六条指令共享一个前台 ELF，由内核进入 EL0 执行，完成后通过 SVC 返回 EL1。

```text
tcc tools
pwd
ls /
cd /src
ls
grep tool_input tools.c
read kernel.c 1 5
cat tools.c
```

`/src/kernel.c` 是当前可自举内核的完整 C 翻译单元，`/src/tools.c` 是独立的工具翻译单元。当前目录可用绝对路径、相对路径、`.` 和 `..`，在同次开机的内核热切换后保留。`grep TEXT FILE` 搜索单个无空格的字面字符串并显示行号。`read FILE FIRST COUNT` 从第 FIRST 行开始显示 COUNT 行，从 1 计数。

在串口执行 `tool source` 查看工具草稿。`tool edit OLD|NEW` 替换其中第一次出现的字面文本。执行 `tcc tools` 会编译草稿，编译成功后将源码保存到独立的双备份磁盘区。`tool reset` 放弃未保存的编辑并重新读取已保存的源码。客机冷启动后按需从源码生成 ELF。可以在内核版本和启动槽保持原值时独立修改工具。工具保存只检查可编译性和 ELF 装载条件，行为需要另外执行测试。

用户程序运行期间，MMU 只允许 EL0 访问它自己的代码、数据、栈和调用缓冲区。代码页只读可执行，可写页禁止执行。命令获得当前源码和参数的副本，通过有界输出缓冲区返回结果。当前只运行一个前台命令。用户指令异常和日常命令超时都会回到提示符；内核候选试运行期间沿用原有看门狗和回退流程。

## 用客机 Agent 迭代内核

默认模型端点是 DeepSeek 的 Chat Completions API。在客机串口输入 `agent key`，在隐藏提示符输入密钥。密钥只在本次开机的客机 RAM 中有效，绑定到输入时的完整 HTTPS URL。切换端点会清除密钥。每个任务先发送只读命令分派函数的源码、需求、验收条件和修复记录。模型可以按函数名请求其他源码，也可以请求完整的当前内核源码。候选内核与常驻服务处于同一地址空间，应使用可信的模型和内核代码。

```text
agent probe
agent test ping|pong
agent run 增加一个只读的 ping 命令，输出 pong 和换行
ping
agent diff
agent verify
```

`agent probe` 发送一个简短模型请求，可以单独检查网络、模型名称和凭据。模型给出对当前源码的精确替换，编辑、TCC 编译、启动候选内核和检查真实串口行为都由客机完成。用户指定的命令和预期输出在该次任务内固定。对一个新的只读查询，也可以使用 `agent query 需求`。先用 `agent test` 指定命令和输出，小模型只需生成对应的 C 表达式，客机负责插入命令分支并验证。没有指定时使用模型提出的第一个行为测试。后续迭代会重跑已通过的命令测试，当前最多保留 12 项。相同命令的新预期在成功提交后替换旧预期。

可以把推理放在一台 Linux 机器上，使用支持 Chat Completions 和 JSON 对象输出的 llama.cpp 或其他服务。在客机输入如下命令并换成服务的实际地址和模型名。模型也可以在运行 QEMU 的同一台机器上提供服务，这种情况下客户机用 `10.0.2.2` 访问宿主机的本地监听端口。明文 HTTP 适用于可信测试网，不发送 API 密钥。

```text
agent model http://192.168.1.20:8080/v1/chat/completions my-coder
agent probe
```

同一台 Linux 主机上也可以离线推理。准备 llama.cpp 的 `llama-server` 和 GGUF 权重，在一个终端启动模型，再在另一个终端运行 `./run.sh`。

```sh
llama-server -m qwen2.5-coder-1.5b-instruct-q4_k_m.gguf -c 24576 --alias coder --host 127.0.0.1 --port 8080
```

在客机执行 `agent model http://10.0.2.2:8080/v1/chat/completions coder`。端点设置会落盘。实测采用 Qwen2.5-Coder-1.5B-Instruct 的 Q4_K_M 权重，SHA-256 为 `cc324af070c2ecbfd324a30884d2f951a7ff756aba85cb811a6ec436933bb046`。小模型能完成简单源码修改，复杂补丁可能用完六轮仍无法通过；可以查看 `agent log`、切换模型并重新提出任务。[模型发布页](https://huggingface.co/Qwen/Qwen2.5-Coder-1.5B-Instruct-GGUF)

- `agent status` 和 `agent log` 显示任务、模型轮数、编译及验收结果。`agent diff` 显示最近一次应用的源码替换。`agent tests` 列出已接受的行为测试，`agent untest 命令名` 移除指定回归项。
- `agent resume` 续接中断的任务和草稿；冷启动后使用有密钥的端点需重新输入 `agent key`。`agent cancel` 结束当前待恢复任务。每个任务最多六轮已返回的模型提案。连接或服务失败时记录错误并暂停。
- 候选内核首先试运行。验收通过才发布其启动槽头。编译错误和行为不符会带着诊断回到模型。试运行卡死时，由客机 ARM 定时器在约八秒后复位，重新启动最近确认的内核；此时可以检查日志并续接。一次正常的热切换会保留备注，定时器复位走冷启动。
- 任务日志和草稿交替写入工作盘的两块区域。源码、候选 ELF 和已验收内核都有独立校验。这里的验收面是串口只读命令的实际输出，包括用户态工具的只读命令。驱动性能、特权代码的安全性和未写入测试的行为需要独立验证。

### DeepSeek 真实验收

在一次性工作盘上，客机通过 HTTPS 直接调用 DeepSeek，任务是新增带整数参数的 `twice N` 命令。第一轮候选内核把 `twice 7` 算成了 `7`，行为验收拒绝提交。第二轮修复后输出 `14` 并热切换。冷启动后另外检查了 `twice 0`、`twice 23` 和未知命令。两个槽中的源码与 ELF 不同。重启后无密钥探测返回 HTTP 401，原密钥字节串未出现在整盘中。

### 启动经过两代真实模型迭代的工作盘

发布包中包含一张示例盘。Qwen2.5-Coder-0.5B 使用查询工具增加了 `color` 命令，Qwen2.5-Coder-1.5B 使用通用源码替换修改了 `note` 的输出标签。第二代提交时，两项回归均通过。冷启动后执行 `color` 会得到 `blue`，执行 `note kept` 再执行 `note` 会得到 `memo=kept`。

示例盘保留 1.1.0 两代模型修改的原始启动状态。要在该盘运行 1.2.0 用户工具，可在关机后用下节的升级命令安装新种子。直接运行该盘时，推理端点已配置上面的本地模型端点，查看结果、执行 `agent verify` 或回滚无需启动模型服务。

```sh
mkdir -p build
(set -C; gzip -dc release/harness-demo.img.gz >build/example.img)
./run.sh build/example.img
```

### 从 1.0.0 或 1.1.0 工作盘升级

先让使用这张盘的虚拟机关机，然后运行 `./run.sh --upgrade /path/to/system.img`。新种子占用备用槽，保留先前的活动内核及其源码，`rollback` 可以切回。升级安装的是 1.2.0 源码，自定义旧源码保存在旧槽；后续再提交新一代会复用备用槽。普通启动旧盘会继续运行盘中已安装的版本。

### 自我迭代的边界

`src/kernel.c` 和 `src/harness.c` 随源码一起进入客机，模型可以修改命令、驱动和 Harness 本身。VirtIO 网卡、IPv4/DNS/TCP、TLS 客户端和模型协议适配器位于常驻镜像中。更换这些组件、TCC 或加载器仍需通过 `build.sh` 制作新种子。Linux 可使用 AArch64 裸机交叉工具链完成这个步骤。推理服务需要可达，客机通过 QEMU 用户网络直接连接服务，全程使用客机的网络协议栈、RTC、随机数设备和证书验证。

## 自举和切换

- **种子和工具。** `build.sh` 用 Arm GNU 工具链构建种子内核、常驻 ELF 加载器和上游 TinyCC 0.9.28rc 的 AArch64 编译库。构建脚本固定 TinyCC 提交 `f830a1697e159056b75de40a60464bc43ee670a3`。工具库在客机 CPU 上完成预处理、编译和链接，输出写入客机内存。这版自举的对象是 `src/abi.h` 和 `src/kernel.c` 组成的完整内核。常驻加载器、TinyCC 和其 C 运行库由启动镜像提供。
- **内核职责。** 可替换的内核实现 PL011 串口、VirtIO 块设备驱动、双槽安装与回滚、源码编辑和命令循环。加载器校验 AArch64 ELF 和段的内存边界，装入段、清零 BSS、重置栈并同步指令缓存。新内核重新初始化设备。
- **磁盘提交。** 内核将 ELF 和源码写到备用槽，刷盘并读回核验，再写入带代数和校验值的槽头并刷盘。冷启动按提交代数选择有效槽。最近的副本校验失败时，尝试另一槽；两槽均不可用时，启动镜像仍提供种子内核。校验覆盖意外损坏。手动 `switch` 直接发布编译产物。Agent 走带验收和定时器恢复的试运行路径。
- **在线状态。** 交接结构保留备注和本次开机的切换次数。冷启动会重置这两项。当前采用单核和单个前台 EL0 程序。热切换发生在内核提示符或 harness 交接点。

## 从源码构建与验证

```sh
./build.sh
python3 test.py
python3 test_harness.py
python3 test_user.py
```

宿主构建需要 Git、Python 3、C 编译器和带 newlib 的 `aarch64-none-elf-gcc`。Apple Silicon 上可以自动下载并校验 Arm GNU 14.3.Rel1。其他平台在 PATH 中提供同目标的工具链。`build/boot.bin` 存在时，启动器优先使用源码构建产物；干净检出使用经 SHA-256 校验的 `release/boot.bin`。

测试在临时磁盘上运行真实 QEMU，覆盖两代客机编译、源码修改生效、同一虚拟机内交接、失败构建隔离、冷启动、回滚持久化和损坏槽回退。关机后，测试还从镜像独立读取两代产物，检查 AArch64 ELF 标识及配套源码。


`test_harness.py` 使用本地 HTTP 模型协议测试端，覆盖错误修复、独立行为验收、跨任务回归、请求失败、冷启动续接及卡死恢复。`test_live.py` 用真实模型在一次性盘上改变 `note` 命令的输出标签并冷启动核验。可传 `--url` 和 `--model`，用 `--query-only` 验证由小模型添加 `color` 查询命令，HTTPS 密钥通过环境变量 `LIXINOS_MODEL_KEY` 提供。

1.2.0 发布镜像在临时的 AArch64 Alpine Linux 3.21 虚拟机内，用发行版的 QEMU 9.1.2 跑通了 `test.py`、`test_harness.py` 和 `test_user.py`。最后一组测试还核验了用户态故障、超时返回、工具源码冷启动恢复，以及旧工作盘升级后回滚。

网络实现使用 lwIP 提交 `0a0452b2c39bdd91e252aef045c115f88f6ca773`、BearSSL 0.6 和 cJSON 1.7.19。对应源码包含在 `third_party/`。构建使用内置 CA 公钥表 `net/anchors.h`，由 BearSSL 的 `brssl ta` 从本次发布的 Mozilla CA 集合生成。

# LixinOS 1.6.0

LixinOS 是 AArch64 裸机 C 系统。发布包的启动 ELF 由客机内的 TinyCC 编译。启动时挂载随包的源码卷，客机可以重建自己的内核、TinyCC、ELF 加载器、网络栈、TLS 库和用户态程序。

## 运行

Linux 或 macOS 上准备 QEMU 的 `qemu-system-aarch64`、POSIX Shell 及 `sha256sum` 或 `shasum` 即可启动。发布目录含预编译启动镜像。手动编译、切换、回滚和验收都可以离线完成。

```sh
./run.sh
```

启动器默认使用 `build/system.img`，首次使用时创建 25 MiB 的空磁盘。也可以传入一个新的路径，例如 `./run.sh build/experiment.img`。在客机提示符下输入下列命令。

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

## 在客机里从源码重建

`/src` 挂载随包的源码卷。可以离线用 `ls`、`cd`、`grep` 和 `read` 查看其中的目录和文件。下列位置覆盖各个构建目标。

| 路径 | 内容 |
|---|---|
| `/src/kernel/`、`/src/kernel.c` | 初始内核模块、交接 ABI、harness 和可编辑的当前内核翻译单元 |
| `/src/boot/` | AArch64 复位入口、异常交接、页表、ELF 加载和系统构建代码 |
| `/src/tinycc/`、`/src/bootstrap/`、`/src/musl/`、`/src/sdk/` | TinyCC 源码、目标配置、AArch64 编译运行库、系统 C 运行库和头文件 |
| `/src/net/`、`/src/lwip/`、`/src/bearssl/`、`/src/cjson/` | VirtIO 网络、TCP/IP、TLS、信任锚、JSON 和模型协议 |
| `/src/user/`、`/src/tools.c` | 用户态 ABI、C 程序入口、示例应用及可编辑的当前命令工具源码 |
| `/src/system.build`、`/src/apps.build` | 系统启动 ELF 的逐文件构建清单和用户程序目标清单。应用自己的 `.build` 文件可列出多个 C 或汇编文件 |

在 `kernel>` 下执行一轮完整构建。`tcc all` 检查源码卷中 /src/apps.build 列出的用户程序、编出候选内核，并把包含 TinyCC 和网络栈的新系统 ELF 写到工作盘。`switch` 在线接管候选内核。关机后再启动，会装入工作盘中最近一次校验通过的系统 ELF。

```text
cat /src/apps.build
tcc all
switch
off
```

可以分别运行 `tcc kernel`、`tcc tools`、`tcc system`。`tcc recovery` 编译可独立冷启动的恢复 ELF，并写入单独的导出区。恢复 ELF 与常规系统 ELF 使用不同的装载地址，使恢复镜像能够装入工作盘里的系统。系统 ELF 在两个磁盘区域轮换，写入和刷盘后读回验证，再提交记录。启动时检查源码卷标识、记录和 ELF 内容，最新副本损坏时尝试上一份。

普通前台程序可以使用 `main(argc, argv)` 和客机的输出接口。下例会由客机 TCC 分别编译用户态启动代码和应用，再链接成 ELF。`run` 执行最近显式选中的应用。

```text
cat /src/user/hello.build
tcc /src/user/hello.build
run LixinOS
```

预期输出是 `hello LixinOS`。带独立 `_start` 的单文件程序也可以直接执行 `tcc /src/路径/文件.c`。构建清单里的路径使用客机绝对路径，每行一个文件。源码卷作为只读构建输入发布；当前内核和命令工具的客机修改分别保存在工作盘，`/src/kernel.c` 和 `/src/tools.c` 会显示当前内容。

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

用户程序运行期间，MMU 向 EL0 开放程序自己的代码、数据、栈和调用缓冲区，以及只读映射的源码卷。代码页只读可执行，可写页禁止执行。命令获得当前源码和参数的副本，通过有界输出缓冲区返回结果。当前只运行一个前台命令。用户指令异常和日常命令超时都会回到提示符；内核候选试运行期间沿用原有看门狗和回退流程。

## 跨任务安装与复用用户程序

新盘带有文件读取接口和验收数据。第一次任务在客机提示符输入以下命令。使用 HTTPS 模型时，先用 `agent key` 输入本次开机的凭据。

```text
agent task /src/validation/lines.tests 实现 Unix 中按换行字节统计文件行数的常用命令，接受 -l FILE，分块读取，只输出个数和换行
apps info wc
cd /work
ls
cat wc.build
wc -l wc.c
off
```

重新启动同一块磁盘，再次用 `agent key` 输入凭据。新任务只接收当前任务和已安装命令的用途。模型选择调用现有命令或提出缺少的命令名，创建时使用任务指定的客机验收文件。客机展开末尾形如 `/src/user/*.c` 的参数，逐文件调用模型选中的 EL0 程序。直接从串口按命令名运行程序可以离线完成。

```text
apps stats
agent task 统计 /src/user/ 下各 C 文件的行数
apps stats
```

`apps` 和 `help` 列出已安装命令。`apps info NAME` 显示用例、版本以及源码输入、完整源码卷和 ELF 的 SHA-256。`apps stats` 记录本次开机的应用编译次数、草稿写入次数和已安装命令的执行次数。恢复 ELF 冷启动时仍会用客机 TCC 准备启动内核。

`agent make NAME /src/FILE.tests GOAL` 将模型给出的 C 源码和生成的构建清单一起存入工作盘。每行用例写成 `参数|预期单行输出`，客机还检查退出状态和行尾换行。全部通过后才提交新版本，安装记录交替写入两个磁盘区。验收失败时已安装版本仍可运行。`apps resume` 用已保存的草稿重新编译验收。当前工作盘最多安装四条命令，另存一份最近的草稿，每个 C 文件限 8191 字节。SDK 的 `file_open`、`file_read` 和 `file_close` 可读取源码卷和工作区文件，支持相对当前目录的路径。

`python3 test_packages.py` 使用固定模型响应做可重复测试。`python3 test_packages.py --deepseek-keychain` 在 macOS 上从原有钥匙串读取凭据，让两次启动的客机分别通过 HTTPS 调用 DeepSeek。宿主独立计数、比较两轮的内核槽、核对磁盘上的 ELF 哈希，并检查一次性系统盘是否包含密钥字节。

1.4.0 的 DeepSeek 实测从空工具目录开始。模型提出 `wc` 并编写源码，EL0 的三个用例通过。第二次启动以新任务发现同一份 ELF，输出 `crt.c 60`、`hello.c 7` 和 `tools.c 109`。客机记录本轮应用编译 0 次、源码写入 0 次、执行 3 次。实测 ELF 的 SHA-256 为 `007b64db75f4922080f8dc0986b2ab36554d4510c6c9549cc3604646c5ff1f34`。重新向模型请求源码时可能得到另一个通过用例的 ELF。

## 用已有工具组合出可验证的结果

1.5.0 内置了一个针对 `/src/user/*.c` 的“选出行数最多的文件”任务协议。从已经安装 `wc` 的 1.4.0 工作盘开始，关机后用 `./run.sh --upgrade 工作盘路径` 升级。在新系统输入 `apps recheck wc`。客机加载旧 ELF，重跑安装时记录的用例，登记这份 ELF 与新源码卷的兼容性。命令保留原来的构建记录和 ELF 哈希。对于新系统中已经验收的 `wc`，可直接执行任务。

```text
agent key
agent task 找出 /src/user/ 里行数最多的 C 文件
cat /work/counts.txt
cat /work/answer.txt
result info
result verify
```

模型从已安装目录选择统计工具。客机逐文件执行它；缺少 `max` 时，由模型生成用户态 C 程序，客机 TCC 编译并用普通最大值、并列和零值用例验收。新程序读取计数文件，输出获胜路径和行数，并列时选字典序最小的路径。客机另外编译 `/src/validation/longest.build`，用这个独立的 EL0 程序枚举目录并逐文件核对换行字节。检查通过后，计数、答案及三个 ELF 的哈希一起保存。重启后可以离线运行 `result verify`。工作盘保留最近两代结果。计数文件容量为 8191 字节。

下列操作在客机里制作一份故意漏掉文件的输入，保持原答案不动。最后一条命令应报出缺少 `/src/user/hello.c`。`scratch.txt` 只在本次开机有效，已提交的两份结果文件仍可按原路径读取。

```text
tcc /src/validation/omit.build
run /work/counts.txt /src/user/hello.c > /work/scratch.txt
result verify /work/scratch.txt /work/answer.txt
```

`python3 test_composition.py` 用本地 Git 的 `v1.4.0` 镜像准备旧盘，使用固定源码在旧客机里编译安装同一份 `wc`，验证升级、旧 ELF 复用、结果冷启动和漏文件拒绝。使用 `--deepseek-keychain` 时，升级后由客机直连 DeepSeek 选择组合、补写 `max`。默认 DeepSeek Flash 端点的工具选择和用户程序生成使用关闭思考的 JSON 输出，给 C 源码保留响应预算。

1.5.0 的真实 DeepSeek 验收复用了 1.4.0 安装的同一份 `wc` ELF（SHA-256 前缀 `328fd8231288`），生成并安装了 `max`（`2512f8cfa5fd`）。客机核验了 `crt.c 68`、`hello.c 7`、`tools.c 112`。删去 `hello.c` 的计数后获胜路径仍是 `tools.c`，同一验证器拒绝缺行的输入，并再次接受原始结果。

## 在同一个镜像上安装不同的流程

1.6.0 通过 `/src/flows/*.flow` 和工作盘上显式安装的清单发现流程。模型收到已安装命令和可用流程的用途，只需返回流程名。客机按清单逐文件运行 `map` 命令，将结果交给 `reduce` 程序，再用 `verify` 指定的 EL0 验证器检查两个文件。每个流程目前有两个命令步骤，结果路径是 `/work/counts.txt` 和 `/work/answer.txt`。首次缺少归约程序时，客机把清单中的 `implement` 和 `tests` 交给用户程序创建入口。工作盘记录所用清单和验证器的 SHA-256。

清单按行依次列出 `name`、`purpose`、`map`、`reduce`、`tests`、`implement` 和 `verify`。发布源码卷带有完整示例 `/src/flows/longest.flow`。第二个流程的独立安装包在检出目录的 `examples/total/`。在已装 `wc` 的客机里，用 `work put /work/文件名` 依次写入该包中的 `sum.tests`、`checktotal.c`、`checktotal.build` 和 `total.flow`。每次粘贴文件内容后另起一行输入一个句点结束。客机可保存八个具名文本文件，单个文件容量是 4095 字节，写入后内容固定。用新的文件名和流程名登记修订版本。流程安装和验证器编译由客机完成。

```text
flow install /work/total.flow
flow list
agent key
agent task 统计 /src/user/ 下所有 C 文件的总行数，只给一个合计数字
result info
result verify
```

安装的流程和程序均可跨冷启动复用。在 `sum` 已经生成并通过验收的盘上，`flow run total` 可以离线执行。串口安装包由操作者提供清单、用例和独立验证器，真实模型负责选择流程并编写 `sum`。运行 `python3 test_flows.py` 可重现安装、断电记录损坏回退和漏文件验收。加 `--deepseek-keychain` 可从客机直连 DeepSeek 运行新任务。验收使用冻结的测试源码卷，额外包含一个零字节的 `/src/user/empty.c`。三个其他 C 文件共 187 行，故意漏掉空文件时，独立验证器应报出缺失的文件名。

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

示例盘保留 1.1.0 两代模型修改的原始启动状态。要在该盘运行 1.6.0 用户工具，可在关机后用下节的升级命令安装新种子。直接运行该盘时，推理端点已配置上面的本地模型端点，查看结果、执行 `agent verify` 或回滚无需启动模型服务。

```sh
mkdir -p build
(set -C; gzip -dc release/harness-demo.img.gz >build/example.img)
./run.sh build/example.img
```

### 从 1.0.0、1.1.0 或 1.2.0 工作盘升级

先让使用这张盘的虚拟机关机，然后运行 `./run.sh --upgrade /path/to/system.img`。新种子占用备用槽，保留先前的活动内核及其源码，`rollback` 可以切回。升级安装的是 1.6.0 源码，自定义旧源码保存在旧槽；后续再提交新一代会复用备用槽。普通启动旧盘会继续运行盘中已安装的版本。回滚到旧版本后，用同一条 `--upgrade` 命令重新进入 1.6.0。显式升级会将原有 1 MiB 工作盘扩展到 25 MiB。升级后使用支持完整源码卷的工具；1.2.0 保存过的旧工具可用 `tool previous` 查看。

### 自我迭代的边界

模型任务目前修改活动内核和 harness 的 C 翻译单元。用户工具支持单独编辑、编译和验收。整机重建读取已发布源码卷中的启动、编译器和网络库源码；这些目录的常规文件编辑器尚未接入客机。推理服务通过客机的网络协议栈访问。


## 自举和切换

- **启动和编译。** 日常启动加载客机构建的 `release/boot.elf` 和 `release/sources.img`。恢复 ELF 在空盘上使用内部 TinyCC 按 `/src/kernel/main.c` 编译内核。客机已编辑过的内核使用其当前翻译单元重建。完全从宿主源码检出开始时，`build.sh` 用 Arm GNU 工具链制作一次引导种子，并固定 TinyCC 提交 `f830a1697e159056b75de40a60464bc43ee670a3`。
- **内核职责。** 可替换的内核实现 PL011 串口、VirtIO 块设备驱动、双槽安装与回滚、源码编辑和命令循环。加载器校验 AArch64 ELF 和段的内存边界，装入段、清零 BSS、重置栈并同步指令缓存。新内核重新初始化设备。
- **磁盘提交。** 内核将 ELF 和源码写到备用槽，刷盘并读回核验，再写入带代数和校验值的槽头并刷盘。冷启动按提交代数选择有效槽。最近的副本校验失败时，尝试另一槽；两槽均不可用时，启动镜像仍提供种子内核。校验覆盖意外损坏。手动 `switch` 直接发布编译产物。Agent 走带验收和定时器恢复的试运行路径。
- **在线状态。** 交接结构保留备注和本次开机的切换次数。冷启动会重置这两项。当前采用单核和单个前台 EL0 程序。热切换发生在内核提示符或 harness 交接点。

## 从源码构建与验证

```sh
python3 test.py
python3 test_harness.py
python3 test_user.py
python3 test_bootstrap.py
```

上面的测试直接运行发布镜像。开发者要从宿主源码生成最初的种子时，可以运行 `./build.sh` 和 `./run.sh --seed`。这一步使用 Git、Python 3、C 编译器及带 newlib 的 `aarch64-none-elf-gcc`。要由这个种子生成新的发布启动 ELF，可执行 `python3 test_bootstrap.py --seed build/boot.bin --sources build/sources.img --export-recovery build/recovery.elf`。导出的是客机编译、写入磁盘并校验过的 ELF。

测试在临时磁盘上运行真实 QEMU，覆盖两代客机编译、源码修改生效、同一虚拟机内交接、失败构建隔离、冷启动、回滚持久化和损坏槽回退。关机后，测试还从镜像独立读取两代产物，检查 AArch64 ELF 标识及配套源码。


`test_harness.py` 使用本地 HTTP 模型协议测试端，覆盖错误修复、独立行为验收、跨任务回归、请求失败、冷启动续接及卡死恢复。`test_live.py` 用真实模型在一次性盘上改变 `note` 命令的输出标签并冷启动核验。可传 `--url` 和 `--model`，用 `--query-only` 验证由小模型添加 `color` 查询命令，HTTPS 密钥通过环境变量 `LIXINOS_MODEL_KEY` 提供。

`test_bootstrap.py` 从客机磁盘读出新编译的系统 ELF，再用该 ELF 和源码卷启动一张空盘。新系统自己的 TinyCC 能编译用户程序并重复整机构建。1.3.0 验收中，三份常规系统 ELF 逐字节一致，各为 1,364,020 字节。在当前 Mac 上启动的 AArch64 Alpine Linux 3.21 虚拟机内，发行版 QEMU 9.1.2 重跑了四组发布测试并得到相同的系统 ELF 哈希。测试也核验了 binary64 和 binary128 浮点常量、系统副本损坏回退，以及恢复 ELF 的冷启动。

网络实现使用 lwIP 提交 `0a0452b2c39bdd91e252aef045c115f88f6ca773`、BearSSL 0.6 和 cJSON 1.7.19。对应源码包含在 `third_party/`。构建使用内置 CA 公钥表 `net/anchors.h`，由 BearSSL 的 `brssl ta` 从本次发布的 Mozilla CA 集合生成。

自举运行库使用 musl 1.2.5 的相关源码及头文件，并使用 stb_sprintf 提供格式化输出。两者的源码和许可均随源码卷发布。

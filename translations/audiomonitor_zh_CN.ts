<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="zh_CN">
<context>
    <name>AudioRouterLinux</name>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="136"/>
        <source>Unable to create the PipeWire thread loop</source>
        <translation>无法创建 PipeWire 线程循环</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="144"/>
        <source>Unable to create the PipeWire context</source>
        <translation>无法创建 PipeWire 上下文</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="152"/>
        <source>Unable to start the PipeWire thread loop: %1</source>
        <translation>无法启动 PipeWire 线程循环：%1</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="172"/>
        <source>Unable to connect to the PipeWire server</source>
        <translation>无法连接 PipeWire 服务器</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="186"/>
        <source>Unable to obtain the PipeWire registry</source>
        <translation>无法获取 PipeWire 注册表</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="209"/>
        <location filename="../src/core/audiorouter_linux.cpp" line="244"/>
        <source>PipeWire is not connected</source>
        <translation>PipeWire 未连接</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="249"/>
        <source>The monitoring volume is not a finite number</source>
        <translation>监听音量不是有限数值</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="266"/>
        <source>The selected PipeWire source or target sink is no longer available</source>
        <translation>所选 PipeWire 监听源或目标接收器已不可用</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="274"/>
        <source>The source and target sinks must be different to prevent feedback</source>
        <translation>监听源和目标接收器必须不同，以免形成反馈回路</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="298"/>
        <source>The selected source sink does not expose FL/FR monitor ports</source>
        <translation>所选监听源接收器未提供 FL/FR 监听端口</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="306"/>
        <source>The selected target sink does not expose FL/FR input ports</source>
        <translation>所选目标接收器未提供 FL/FR 输入端口</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="329"/>
        <source>PipeWire did not publish the monitoring filter node</source>
        <translation>PipeWire 未发布监听过滤器节点</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="346"/>
        <source>PipeWire did not publish all FL/FR monitoring filter ports</source>
        <translation>PipeWire 未发布监听过滤器的所有 FL/FR 端口</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="548"/>
        <source>PipeWire core error: %1</source>
        <translation>PipeWire 核心错误：%1</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="680"/>
        <source>The selected PipeWire source or target sink was removed; monitoring stopped</source>
        <translation>所选 PipeWire 监听源或目标接收器已移除，监听已停止</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="717"/>
        <source>unknown error</source>
        <translation>未知错误</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="718"/>
        <source>PipeWire filter error: %1</source>
        <translation>PipeWire 过滤器错误：%1</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="731"/>
        <source>The PipeWire monitoring filter disconnected; monitoring stopped</source>
        <translation>PipeWire 监听过滤器已断开，监听已停止</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="780"/>
        <source>A PipeWire monitoring link was destroyed</source>
        <translation>PipeWire 监听链接已销毁</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="789"/>
        <source>A PipeWire monitoring link was removed</source>
        <translation>PipeWire 监听链接已移除</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="801"/>
        <source>PipeWire could not create a monitoring link: %1</source>
        <translation>PipeWire 无法创建监听链接：%1</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="815"/>
        <source>PipeWire monitoring link error: %1</source>
        <translation>PipeWire 监听链接错误：%1</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="912"/>
        <source>The PipeWire core disconnected</source>
        <translation>PipeWire 核心已断开</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="920"/>
        <source>Unable to synchronize with PipeWire: %1</source>
        <translation>无法与 PipeWire 同步：%1</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="930"/>
        <source>Unable to create a PipeWire synchronization deadline: %1</source>
        <translation>无法创建 PipeWire 同步超时期限：%1</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="940"/>
        <source>Timed out while synchronizing with PipeWire</source>
        <translation>与 PipeWire 同步时超时</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="945"/>
        <source>PipeWire synchronization failed: %1</source>
        <translation>PipeWire 同步失败：%1</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="972"/>
        <source>Unable to allocate PipeWire filter properties</source>
        <translation>无法分配 PipeWire 过滤器属性</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="979"/>
        <source>Unable to create the PipeWire monitoring filter</source>
        <translation>无法创建 PipeWire 监听过滤器</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="1002"/>
        <source>Unable to create PipeWire filter input %1</source>
        <translation>无法创建 PipeWire 过滤器输入 %1</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="1024"/>
        <source>Unable to create PipeWire filter output %1</source>
        <translation>无法创建 PipeWire 过滤器输出 %1</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="1046"/>
        <source>Unable to connect the PipeWire monitoring filter: %1</source>
        <translation>无法连接 PipeWire 监听过滤器：%1</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="1060"/>
        <source>Unable to create a PipeWire filter deadline: %1</source>
        <translation>无法创建 PipeWire 过滤器超时期限：%1</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="1072"/>
        <source>Timed out while activating the PipeWire monitoring filter</source>
        <translation>激活 PipeWire 监听过滤器时超时</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="1078"/>
        <source>Waiting for the PipeWire filter failed: %1</source>
        <translation>等待 PipeWire 过滤器失败：%1</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="1089"/>
        <source>The PipeWire monitoring filter entered an error state</source>
        <translation>PipeWire 监听过滤器进入错误状态</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="1110"/>
        <source>Unable to allocate PipeWire link properties</source>
        <translation>无法分配 PipeWire 链接属性</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="1127"/>
        <source>Unable to populate PipeWire monitoring link properties</source>
        <translation>无法填充 PipeWire 监听链接属性</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="1142"/>
        <source>Unable to create a PipeWire monitoring link</source>
        <translation>无法创建 PipeWire 监听链接</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="1158"/>
        <source>Unable to create a PipeWire link deadline: %1</source>
        <translation>无法创建 PipeWire 链接超时期限：%1</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="1167"/>
        <source>Timed out while negotiating PipeWire monitoring links</source>
        <translation>协商 PipeWire 监听链接时超时</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="1173"/>
        <source>Waiting for PipeWire monitoring links failed: %1</source>
        <translation>等待 PipeWire 监听链接失败：%1</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="1187"/>
        <source>A PipeWire monitoring link did not become active</source>
        <translation>PipeWire 监听链接未进入活动状态</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="1497"/>
        <source>Debug audio dumps are disabled on the native PipeWire backend because file I/O is not realtime-safe</source>
        <translation>原生 PipeWire 后端已禁用调试音频转储，因为文件 I/O 不符合实时安全要求</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="1545"/>
        <source>Capture dumps are disabled on the native PipeWire backend because file I/O is not realtime-safe</source>
        <translation>原生 PipeWire 后端已禁用捕获转储，因为文件 I/O 不符合实时安全要求</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="1555"/>
        <source>Playback dumps are disabled on the native PipeWire backend because file I/O is not realtime-safe</source>
        <translation>原生 PipeWire 后端已禁用播放转储，因为文件 I/O 不符合实时安全要求</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="1565"/>
        <source>Callback dumps are disabled on the native PipeWire backend because file I/O is not realtime-safe</source>
        <translation>原生 PipeWire 后端已禁用回调转储，因为文件 I/O 不符合实时安全要求</translation>
    </message>
</context>
<context>
    <name>AudioRouterWin</name>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="491"/>
        <source>The %1 worker failed to start; monitoring stopped</source>
        <translation>%1 工作线程启动失败，监听已停止。</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="648"/>
        <source>Capture</source>
        <translation>捕获</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="846"/>
        <source>Render</source>
        <translation>播放</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="663"/>
        <source>Waiting for the capture event failed; monitoring stopped</source>
        <translation>等待捕获事件失败，监听已停止。</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="369"/>
        <source>Unable to create the stop event</source>
        <translation>无法创建停止事件</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="375"/>
        <source>Unable to create the start event</source>
        <translation>无法创建启动事件</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="386"/>
        <source>Timed out while initializing the source device</source>
        <translation>初始化监听源设备超时</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="404"/>
        <source>Timed out while initializing the target device</source>
        <translation>初始化转发目标设备超时</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="420"/>
        <source>Unable to start the audio worker threads</source>
        <translation>无法启动音频工作线程</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="502"/>
        <source>Unable to initialize capture-thread COM (%1)</source>
        <translation>无法初始化捕获线程 COM（%1）</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="539"/>
        <location filename="../src/core/audiorouter_win.cpp" line="789"/>
        <source>Unable to create MMDeviceEnumerator (%1)</source>
        <translation>无法创建 MMDeviceEnumerator（%1）</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="544"/>
        <source>Source device was not found: %1 (%2)</source>
        <translation>找不到监听源设备：%1（%2）</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="550"/>
        <source>Unable to activate the source device (%1)</source>
        <translation>无法激活监听源设备（%1）</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="555"/>
        <source>Unable to obtain the source mix format (%1)</source>
        <translation>无法获取监听源设备的混音格式（%1）</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="566"/>
        <source>The source device reported an invalid channel count: %1</source>
        <translation>监听源设备报告的声道数无效：%1</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="571"/>
        <source>The source device reported an invalid sample rate</source>
        <translation>监听源设备报告的采样率无效</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="574"/>
        <source>The source format is unsupported (%1-bit PCM, %2 channels)</source>
        <translation>不支持监听源格式（%1 位 PCM，%2 声道）</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="586"/>
        <source>Unable to create the audio queue: %1</source>
        <translation>无法创建音频队列：%1</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="593"/>
        <source>Unable to create the capture event</source>
        <translation>无法创建捕获事件</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="603"/>
        <source>Loopback capture initialization failed (%1). The device may be unavailable or held in exclusive mode.</source>
        <translation>回环捕获初始化失败（%1）。设备可能不可用或正被独占使用。</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="609"/>
        <location filename="../src/core/audiorouter_win.cpp" line="830"/>
        <source>SetEventHandle failed (%1)</source>
        <translation>SetEventHandle 失败（%1）</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="614"/>
        <source>Unable to obtain the capture buffer size (%1)</source>
        <translation>无法获取捕获缓冲区大小（%1）</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="619"/>
        <source>Unable to obtain the capture client (%1)</source>
        <translation>无法获取捕获客户端（%1）</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="635"/>
        <source>Unable to allocate capture conversion buffers: %1</source>
        <translation>无法分配捕获转换缓冲区：%1</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="642"/>
        <source>Unable to start capture (%1)</source>
        <translation>无法启动捕获（%1）</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="679"/>
        <source>The source device became unavailable; monitoring stopped</source>
        <translation>监听源设备已不可用，监听已停止。</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="695"/>
        <source>Reading capture data failed; monitoring stopped</source>
        <translation>读取捕获数据失败，监听已停止。</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="738"/>
        <source>Releasing capture data failed; monitoring stopped</source>
        <translation>释放捕获数据失败，监听已停止。</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="758"/>
        <source>Unable to initialize render-thread COM (%1)</source>
        <translation>无法初始化播放线程 COM（%1）</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="800"/>
        <source>Target device was not found: %1 (%2)</source>
        <translation>找不到转发目标设备：%1（%2）</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="806"/>
        <source>Unable to activate the target device (%1)</source>
        <translation>无法激活转发目标设备（%1）</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="811"/>
        <source>Unable to create the render event</source>
        <translation>无法创建播放事件</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="820"/>
        <source>The target device rejected the audio format (%1)</source>
        <translation>转发目标设备拒绝了该音频格式（%1）</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="825"/>
        <source>Unable to obtain the render buffer size (%1)</source>
        <translation>无法获取播放缓冲区大小（%1）</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="835"/>
        <source>Unable to obtain the render client (%1)</source>
        <translation>无法获取播放客户端（%1）</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="840"/>
        <source>Unable to start rendering (%1)</source>
        <translation>无法启动播放（%1）</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="861"/>
        <source>Waiting for the render event failed; monitoring stopped</source>
        <translation>等待播放事件失败，监听已停止。</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="874"/>
        <source>The target device became unavailable; monitoring stopped</source>
        <translation>转发目标设备已不可用，监听已停止。</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="888"/>
        <source>Obtaining the render buffer failed; monitoring stopped</source>
        <translation>获取播放缓冲区失败，监听已停止。</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="903"/>
        <source>Submitting the render buffer failed; monitoring stopped</source>
        <translation>提交播放缓冲区失败，监听已停止。</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="1131"/>
        <source>Output device %1</source>
        <translation>输出设备 %1</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="1141"/>
        <source>Monitoring volume must be a finite number</source>
        <translation>监听音量必须是有限数值。</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="1146"/>
        <source>The source and target must be valid devices</source>
        <translation>监听源和转发目标必须是有效设备。</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="1151"/>
        <source>The source and target must be different to prevent an audio feedback loop</source>
        <translation>监听源和转发目标不能是同一设备，以免形成音频反馈回路。</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="1227"/>
        <source>The audio device in use was removed or disabled</source>
        <translation>正在使用的音频设备已移除或停用。</translation>
    </message>
</context>
<context>
    <name>MainWindow</name>
    <message>
        <location filename="../src/mainwindow.cpp" line="285"/>
        <source>Listen source (output device):</source>
        <translation>监听源（输出设备）：</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="287"/>
        <source>Forward to (output device):</source>
        <translation>转发到（输出设备）：</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="289"/>
        <source>Monitoring volume:</source>
        <translation>监听音量：</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="291"/>
        <source>Refresh devices</source>
        <translation>刷新设备</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="293"/>
        <source>The listen source is the output device that is currently playing audio. Its audio is forwarded to the target device in real time.</source>
        <translation>监听源是当前正在播放声音的输出设备，其音频会实时转发到目标设备。</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="297"/>
        <location filename="../src/mainwindow.cpp" line="301"/>
        <location filename="../src/mainwindow.cpp" line="580"/>
        <location filename="../src/mainwindow.cpp" line="581"/>
        <source>Stop monitoring</source>
        <translation>停止监听</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="297"/>
        <location filename="../src/mainwindow.cpp" line="301"/>
        <location filename="../src/mainwindow.cpp" line="592"/>
        <location filename="../src/mainwindow.cpp" line="593"/>
        <source>Start monitoring</source>
        <translation>开始监听</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="299"/>
        <source>Open main window</source>
        <translation>打开主窗口</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="303"/>
        <source>Quit</source>
        <translation>退出</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="305"/>
        <source>Language</source>
        <translation>语言</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="307"/>
        <source>English</source>
        <translation>English</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="309"/>
        <source>Simplified Chinese</source>
        <translation>简体中文</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="312"/>
        <location filename="../src/mainwindow.cpp" line="352"/>
        <source> (default)</source>
        <translation>（默认）</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="419"/>
        <source>Error: no output device is available. Check the audio service.</source>
        <translation>错误：没有可用的输出设备，请检查音频服务。</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="425"/>
        <source>The source and target must be different to prevent an audio feedback loop.</source>
        <translation>监听源和转发目标不能是同一设备，以免形成音频反馈回路。</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="498"/>
        <source>The device is disconnected. Automatic reconnect failed; check the device and try again.</source>
        <translation>设备已断开，自动重连失败，请检查设备后重试。</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="508"/>
        <location filename="../src/mainwindow.cpp" line="601"/>
        <source>AudioMonitor - Reconnecting</source>
        <translation>AudioMonitor - 正在重连</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="517"/>
        <source>Device disconnected; retrying in %1 seconds (attempt %2/%3)...</source>
        <translation>设备已断开，将在 %1 秒后尝试重连（第 %2/%3 次）...</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="561"/>
        <location filename="../src/mainwindow.cpp" line="609"/>
        <source>Error: %1</source>
        <translation>错误：%1</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="563"/>
        <source>AudioMonitor - Error</source>
        <translation>AudioMonitor - 出错</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="587"/>
        <source>Monitoring: %1 -&gt; %2 (continues in the background when the window is closed)</source>
        <translation>正在监听：%1 -&gt; %2（关闭窗口后仍会在后台运行）</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="590"/>
        <source>AudioMonitor - Running: %1 -&gt; %2</source>
        <translation>AudioMonitor - 运行中：%1 -&gt; %2</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="605"/>
        <source>No output devices found. Make sure the audio service (Windows Audio or PipeWire) is running, then refresh the device list.</source>
        <translation>未找到输出设备。请确认音频服务（Windows Audio 或 PipeWire）正在运行，然后刷新设备列表。</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="613"/>
        <source>Stopped. Select devices and click Start monitoring.</source>
        <translation>已停止。选择设备后点击“开始监听”。</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="616"/>
        <source>AudioMonitor - Stopped</source>
        <translation>AudioMonitor - 已停止</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="639"/>
        <source>AudioMonitor</source>
        <translation>AudioMonitor</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="640"/>
        <source>The window was minimized to the system tray; monitoring continues in the background.</source>
        <translation>窗口已最小化到系统托盘，监听会继续在后台运行。</translation>
    </message>
</context>
</TS>

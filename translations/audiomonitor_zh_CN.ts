<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="zh_CN">
<context>
    <name>AudioRouterLinux</name>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="137"/>
        <source>Unable to create the PipeWire thread loop</source>
        <translation>无法创建 PipeWire 线程循环</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="145"/>
        <source>Unable to create the PipeWire context</source>
        <translation>无法创建 PipeWire 上下文</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="153"/>
        <source>Unable to start the PipeWire thread loop: %1</source>
        <translation>无法启动 PipeWire 线程循环：%1</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="173"/>
        <source>Unable to connect to the PipeWire server</source>
        <translation>无法连接 PipeWire 服务器</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="187"/>
        <source>Unable to obtain the PipeWire registry</source>
        <translation>无法获取 PipeWire 注册表</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="210"/>
        <location filename="../src/core/audiorouter_linux.cpp" line="245"/>
        <source>PipeWire is not connected</source>
        <translation>PipeWire 未连接</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="250"/>
        <source>The monitoring volume is not a finite number</source>
        <translation>监听音量不是有限数值</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="267"/>
        <source>The selected PipeWire source or target sink is no longer available</source>
        <translation>所选 PipeWire 监听源或目标接收器已不可用</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="275"/>
        <source>The source and target sinks must be different to prevent feedback</source>
        <translation>监听源和目标接收器必须不同，以免形成反馈回路</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="299"/>
        <source>The selected source sink does not expose FL/FR monitor ports</source>
        <translation>所选监听源接收器未提供 FL/FR 监听端口</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="307"/>
        <source>The selected target sink does not expose FL/FR input ports</source>
        <translation>所选目标接收器未提供 FL/FR 输入端口</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="330"/>
        <source>PipeWire did not publish the monitoring filter node</source>
        <translation>PipeWire 未发布监听过滤器节点</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="347"/>
        <source>PipeWire did not publish all FL/FR monitoring filter ports</source>
        <translation>PipeWire 未发布监听过滤器的所有 FL/FR 端口</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="549"/>
        <source>PipeWire core error: %1</source>
        <translation>PipeWire 核心错误：%1</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="681"/>
        <source>The selected PipeWire source or target sink was removed; monitoring stopped</source>
        <translation>所选 PipeWire 监听源或目标接收器已移除，监听已停止</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="718"/>
        <source>unknown error</source>
        <translation>未知错误</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="719"/>
        <source>PipeWire filter error: %1</source>
        <translation>PipeWire 过滤器错误：%1</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="732"/>
        <source>The PipeWire monitoring filter disconnected; monitoring stopped</source>
        <translation>PipeWire 监听过滤器已断开，监听已停止</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="781"/>
        <source>A PipeWire monitoring link was destroyed</source>
        <translation>PipeWire 监听链接已销毁</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="790"/>
        <source>A PipeWire monitoring link was removed</source>
        <translation>PipeWire 监听链接已移除</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="802"/>
        <source>PipeWire could not create a monitoring link: %1</source>
        <translation>PipeWire 无法创建监听链接：%1</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="816"/>
        <source>PipeWire monitoring link error: %1</source>
        <translation>PipeWire 监听链接错误：%1</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="913"/>
        <source>The PipeWire core disconnected</source>
        <translation>PipeWire 核心已断开</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="921"/>
        <source>Unable to synchronize with PipeWire: %1</source>
        <translation>无法与 PipeWire 同步：%1</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="931"/>
        <source>Unable to create a PipeWire synchronization deadline: %1</source>
        <translation>无法创建 PipeWire 同步超时期限：%1</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="941"/>
        <source>Timed out while synchronizing with PipeWire</source>
        <translation>与 PipeWire 同步时超时</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="946"/>
        <source>PipeWire synchronization failed: %1</source>
        <translation>PipeWire 同步失败：%1</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="973"/>
        <source>Unable to allocate PipeWire filter properties</source>
        <translation>无法分配 PipeWire 过滤器属性</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="980"/>
        <source>Unable to create the PipeWire monitoring filter</source>
        <translation>无法创建 PipeWire 监听过滤器</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="1003"/>
        <source>Unable to create PipeWire filter input %1</source>
        <translation>无法创建 PipeWire 过滤器输入 %1</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="1025"/>
        <source>Unable to create PipeWire filter output %1</source>
        <translation>无法创建 PipeWire 过滤器输出 %1</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="1047"/>
        <source>Unable to connect the PipeWire monitoring filter: %1</source>
        <translation>无法连接 PipeWire 监听过滤器：%1</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="1061"/>
        <source>Unable to create a PipeWire filter deadline: %1</source>
        <translation>无法创建 PipeWire 过滤器超时期限：%1</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="1073"/>
        <source>Timed out while activating the PipeWire monitoring filter</source>
        <translation>激活 PipeWire 监听过滤器时超时</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="1079"/>
        <source>Waiting for the PipeWire filter failed: %1</source>
        <translation>等待 PipeWire 过滤器失败：%1</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="1090"/>
        <source>The PipeWire monitoring filter entered an error state</source>
        <translation>PipeWire 监听过滤器进入错误状态</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="1111"/>
        <source>Unable to allocate PipeWire link properties</source>
        <translation>无法分配 PipeWire 链接属性</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="1128"/>
        <source>Unable to populate PipeWire monitoring link properties</source>
        <translation>无法填充 PipeWire 监听链接属性</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="1143"/>
        <source>Unable to create a PipeWire monitoring link</source>
        <translation>无法创建 PipeWire 监听链接</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="1159"/>
        <source>Unable to create a PipeWire link deadline: %1</source>
        <translation>无法创建 PipeWire 链接超时期限：%1</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="1168"/>
        <source>Timed out while negotiating PipeWire monitoring links</source>
        <translation>协商 PipeWire 监听链接时超时</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="1174"/>
        <source>Waiting for PipeWire monitoring links failed: %1</source>
        <translation>等待 PipeWire 监听链接失败：%1</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="1188"/>
        <source>A PipeWire monitoring link did not become active</source>
        <translation>PipeWire 监听链接未进入活动状态</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="1498"/>
        <source>Debug audio dumps are disabled on the native PipeWire backend because file I/O is not realtime-safe</source>
        <translation>原生 PipeWire 后端已禁用调试音频转储，因为文件 I/O 不符合实时安全要求</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="1546"/>
        <source>Capture dumps are disabled on the native PipeWire backend because file I/O is not realtime-safe</source>
        <translation>原生 PipeWire 后端已禁用捕获转储，因为文件 I/O 不符合实时安全要求</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="1556"/>
        <source>Playback dumps are disabled on the native PipeWire backend because file I/O is not realtime-safe</source>
        <translation>原生 PipeWire 后端已禁用播放转储，因为文件 I/O 不符合实时安全要求</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_linux.cpp" line="1566"/>
        <source>Callback dumps are disabled on the native PipeWire backend because file I/O is not realtime-safe</source>
        <translation>原生 PipeWire 后端已禁用回调转储，因为文件 I/O 不符合实时安全要求</translation>
    </message>
</context>
<context>
    <name>AudioRouterWin</name>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="507"/>
        <source>The %1 worker failed to start; monitoring stopped</source>
        <translation>%1 工作线程启动失败，监听已停止。</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="664"/>
        <source>Capture</source>
        <translation>捕获</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="900"/>
        <source>Render</source>
        <translation>播放</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="679"/>
        <source>Waiting for the capture event failed; monitoring stopped</source>
        <translation>等待捕获事件失败，监听已停止。</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="385"/>
        <source>Unable to create the stop event</source>
        <translation>无法创建停止事件</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="391"/>
        <source>Unable to create the start event</source>
        <translation>无法创建启动事件</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="402"/>
        <source>Timed out while initializing the source device</source>
        <translation>初始化监听源设备超时</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="420"/>
        <source>Timed out while initializing the target device</source>
        <translation>初始化转发目标设备超时</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="436"/>
        <source>Unable to start the audio worker threads</source>
        <translation>无法启动音频工作线程</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="518"/>
        <source>Unable to initialize capture-thread COM (%1)</source>
        <translation>无法初始化捕获线程 COM（%1）</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="555"/>
        <location filename="../src/core/audiorouter_win.cpp" line="806"/>
        <source>Unable to create MMDeviceEnumerator (%1)</source>
        <translation>无法创建 MMDeviceEnumerator（%1）</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="560"/>
        <source>Source device was not found: %1 (%2)</source>
        <translation>找不到监听源设备：%1（%2）</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="566"/>
        <source>Unable to activate the source device (%1)</source>
        <translation>无法激活监听源设备（%1）</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="571"/>
        <source>Unable to obtain the source mix format (%1)</source>
        <translation>无法获取监听源设备的混音格式（%1）</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="582"/>
        <source>The source device reported an invalid channel count: %1</source>
        <translation>监听源设备报告的声道数无效：%1</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="587"/>
        <source>The source device reported an invalid sample rate</source>
        <translation>监听源设备报告的采样率无效</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="590"/>
        <source>The source format is unsupported (%1-bit PCM, %2 channels)</source>
        <translation>不支持监听源格式（%1 位 PCM，%2 声道）</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="602"/>
        <source>Unable to create the audio queue: %1</source>
        <translation>无法创建音频队列：%1</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="609"/>
        <source>Unable to create the capture event</source>
        <translation>无法创建捕获事件</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="619"/>
        <source>Loopback capture initialization failed (%1). The device may be unavailable or held in exclusive mode.</source>
        <translation>回环捕获初始化失败（%1）。设备可能不可用或正被独占使用。</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="625"/>
        <location filename="../src/core/audiorouter_win.cpp" line="847"/>
        <source>SetEventHandle failed (%1)</source>
        <translation>SetEventHandle 失败（%1）</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="630"/>
        <source>Unable to obtain the capture buffer size (%1)</source>
        <translation>无法获取捕获缓冲区大小（%1）</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="635"/>
        <source>Unable to obtain the capture client (%1)</source>
        <translation>无法获取捕获客户端（%1）</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="651"/>
        <source>Unable to allocate capture conversion buffers: %1</source>
        <translation>无法分配捕获转换缓冲区：%1</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="658"/>
        <source>Unable to start capture (%1)</source>
        <translation>无法启动捕获（%1）</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="695"/>
        <source>The source device became unavailable; monitoring stopped</source>
        <translation>监听源设备已不可用，监听已停止。</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="711"/>
        <source>Reading capture data failed; monitoring stopped</source>
        <translation>读取捕获数据失败，监听已停止。</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="754"/>
        <source>Releasing capture data failed; monitoring stopped</source>
        <translation>释放捕获数据失败，监听已停止。</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="774"/>
        <source>Unable to initialize render-thread COM (%1)</source>
        <translation>无法初始化播放线程 COM（%1）</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="817"/>
        <source>Target device was not found: %1 (%2)</source>
        <translation>找不到转发目标设备：%1（%2）</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="823"/>
        <source>Unable to activate the target device (%1)</source>
        <translation>无法激活转发目标设备（%1）</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="828"/>
        <source>Unable to create the render event</source>
        <translation>无法创建播放事件</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="837"/>
        <source>The target device rejected the audio format (%1)</source>
        <translation>转发目标设备拒绝了该音频格式（%1）</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="842"/>
        <source>Unable to obtain the render buffer size (%1)</source>
        <translation>无法获取播放缓冲区大小（%1）</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="852"/>
        <source>Unable to obtain the render client (%1)</source>
        <translation>无法获取播放客户端（%1）</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="886"/>
        <source>Unable to create the adaptive audio reader: %1</source>
        <translation>无法创建自适应音频读取器：%1</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="894"/>
        <source>Unable to start rendering (%1)</source>
        <translation>无法启动播放（%1）</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="915"/>
        <source>Waiting for the render event failed; monitoring stopped</source>
        <translation>等待播放事件失败，监听已停止。</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="928"/>
        <source>The target device became unavailable; monitoring stopped</source>
        <translation>转发目标设备已不可用，监听已停止。</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="942"/>
        <source>Obtaining the render buffer failed; monitoring stopped</source>
        <translation>获取播放缓冲区失败，监听已停止。</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="960"/>
        <source>Submitting the render buffer failed; monitoring stopped</source>
        <translation>提交播放缓冲区失败，监听已停止。</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="1189"/>
        <source>Output device %1</source>
        <translation>输出设备 %1</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="1199"/>
        <source>Monitoring volume must be a finite number</source>
        <translation>监听音量必须是有限数值。</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="1204"/>
        <source>The source and target must be valid devices</source>
        <translation>监听源和转发目标必须是有效设备。</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="1209"/>
        <source>The source and target must be different to prevent an audio feedback loop</source>
        <translation>监听源和转发目标不能是同一设备，以免形成音频反馈回路。</translation>
    </message>
    <message>
        <location filename="../src/core/audiorouter_win.cpp" line="1291"/>
        <source>The audio device in use was removed or disabled</source>
        <translation>正在使用的音频设备已移除或停用。</translation>
    </message>
</context>
<context>
    <name>MainWindow</name>
    <message>
        <location filename="../src/mainwindow.cpp" line="544"/>
        <source>Listen source (output device):</source>
        <translation>监听源（输出设备）：</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="546"/>
        <source>Forward to (output device):</source>
        <translation>转发到（输出设备）：</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="548"/>
        <source>Monitoring volume:</source>
        <translation>监听音量：</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="550"/>
        <source>Refresh devices</source>
        <translation>刷新设备</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="552"/>
        <source>The listen source is the output device that is currently playing audio. Its audio is forwarded to the target device in real time.</source>
        <translation>监听源是当前正在播放声音的输出设备，其音频会实时转发到目标设备。</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="556"/>
        <location filename="../src/mainwindow.cpp" line="560"/>
        <location filename="../src/mainwindow.cpp" line="861"/>
        <location filename="../src/mainwindow.cpp" line="862"/>
        <source>Stop monitoring</source>
        <translation>停止监听</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="556"/>
        <location filename="../src/mainwindow.cpp" line="560"/>
        <location filename="../src/mainwindow.cpp" line="873"/>
        <location filename="../src/mainwindow.cpp" line="874"/>
        <source>Start monitoring</source>
        <translation>开始监听</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="558"/>
        <source>Open main window</source>
        <translation>打开主窗口</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="562"/>
        <source>Quit</source>
        <translation>退出</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="564"/>
        <source>Language</source>
        <translation>语言</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="566"/>
        <source>English</source>
        <translation>English</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="568"/>
        <source>Simplified Chinese</source>
        <translation>简体中文</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="570"/>
        <source>Monitoring mode</source>
        <translation>监听模式</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="572"/>
        <source>Low latency</source>
        <translation>低延迟</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="574"/>
        <source>Stable</source>
        <translation>稳定</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="578"/>
        <location filename="../src/mainwindow.cpp" line="615"/>
        <source> (default)</source>
        <translation>（默认）</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="682"/>
        <source>Error: no output device is available. Check the audio service.</source>
        <translation>错误：没有可用的输出设备，请检查音频服务。</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="688"/>
        <source>The source and target must be different to prevent an audio feedback loop.</source>
        <translation>监听源和转发目标不能是同一设备，以免形成音频反馈回路。</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="761"/>
        <source>The device is disconnected. Automatic reconnect failed; check the device and try again.</source>
        <translation>设备已断开，自动重连失败，请检查设备后重试。</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="771"/>
        <location filename="../src/mainwindow.cpp" line="882"/>
        <source>AudioMonitor - Reconnecting</source>
        <translation>AudioMonitor - 正在重连</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="780"/>
        <source>Device disconnected; retrying in %1 seconds (attempt %2/%3)...</source>
        <translation>设备已断开，将在 %1 秒后尝试重连（第 %2/%3 次）...</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="824"/>
        <location filename="../src/mainwindow.cpp" line="890"/>
        <source>Error: %1</source>
        <translation>错误：%1</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="826"/>
        <source>AudioMonitor - Error</source>
        <translation>AudioMonitor - 出错</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="868"/>
        <source>Monitoring: %1 -&gt; %2 (continues in the background when the window is closed)</source>
        <translation>正在监听：%1 -&gt; %2（关闭窗口后仍会在后台运行）</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="871"/>
        <source>AudioMonitor - Running: %1 -&gt; %2</source>
        <translation>AudioMonitor - 运行中：%1 -&gt; %2</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="886"/>
        <source>No output devices found. Make sure the audio service (Windows Audio or PipeWire) is running, then refresh the device list.</source>
        <translation>未找到输出设备。请确认音频服务（Windows Audio 或 PipeWire）正在运行，然后刷新设备列表。</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="894"/>
        <source>Stopped. Select devices and click Start monitoring.</source>
        <translation>已停止。选择设备后点击“开始监听”。</translation>
    </message>
    <message>
        <location filename="../src/mainwindow.cpp" line="897"/>
        <source>AudioMonitor - Stopped</source>
        <translation>AudioMonitor - 已停止</translation>
    </message>
    <message>
        <source>AudioMonitor</source>
        <translation type="vanished">AudioMonitor</translation>
    </message>
    <message>
        <source>The window was minimized to the system tray; monitoring continues in the background.</source>
        <translation type="vanished">窗口已最小化到系统托盘，监听会继续在后台运行。</translation>
    </message>
</context>
</TS>

# PoggetCore 轻量桌面收纳与整理引擎

PoggetCore 是一款基于 C++20 构建的高性能、跨平台架构的桌面收纳与桌面整理工具。在 Pogget 的实践中，PoggetCore 搭载 [VinaUI](https://github.com/EnderMo/VinaUI) 进行接入，实现轻量、高性能的渲染。

---

## 核心架构分层

PoggetCore 遵循严格的单向依赖与分层设计，将底层数据逻辑与上层渲染引擎彻底解耦：

```mermaid
graph TD
    UI["VinaUI (D3D9/D3D11) "] --> ViewState["UI 视图状态层 Data 引用代理"]
    ViewState --> Core["PoggetCore 逻辑层 (无 GUI 依赖)"]
    Core --> Storage["PoggetStorageProvider (持久化存储)"]
    Core --> CoreMgr["PoggetContainerManager (收纳盒实例管理器)"]
    Core --> ItemMgr["PoggetCoreItemManager (文件元数据管理)"]
```


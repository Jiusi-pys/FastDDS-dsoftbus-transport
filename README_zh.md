# FastDDS dsoftbus Transport

本仓库用于组合 Fast-DDS 与 OpenHarmony `communication_dsoftbus`，为基于
dsoftbus 的 Fast DDS 传输集成开发提供统一的源码快照。

## 目录结构

- `Fast-DDS`：Fast DDS 下游源码仓库。
- `communication_dsoftbus`：OpenHarmony dsoftbus 下游源码仓库。

这两个目录作为 Git submodule 管理。顶层仓库会记录每个下游仓库在当前
集成快照中使用的确切提交，确保源码组合可复现。

## 初始化源码

```bash
git clone --recurse-submodules git@github.com:Jiusi-pys/FastDDS-dsoftbus-transport.git
```

已有工作区可以执行：

```bash
git submodule update --init --recursive
```

如果要有意更新集成快照，请先更新下游仓库，再提交顶层仓库中的 submodule
指针：

```bash
git -C Fast-DDS pull
git -C communication_dsoftbus pull
git add Fast-DDS communication_dsoftbus
git commit -m "Update downstream repositories"
```

## 下游仓库

- Fast DDS：`git@github.com:eProsima/Fast-DDS.git`
- OpenHarmony communication_dsoftbus：`git@gitcode.com:openharmony/communication_dsoftbus.git`

See [README.md](README.md) for the English documentation.

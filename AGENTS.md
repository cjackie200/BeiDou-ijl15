# Repository Guidelines

## ijl15 编译环境

本仓库是 Windows MSVC C++ DLL 工程，不是 WSL g++ / clang 工程。WSL 只负责调用 Windows
构建工具。不要因为 `where msbuild` 或 `dotnet msbuild` 失败就判断本机不能编译；优先使用
当前可用的 VS 2022 BuildTools 完整路径。

推荐从 WSL 执行：

```bash
powershell.exe -NoProfile -Command '& "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" "D:\Game\BeiDou\BeiDou-ijl15\ezorsia.sln" /p:Configuration=Release /p:Platform=x86 /m:1'
```

solution 的 `Release|x86` 映射到项目 `Release|Win32`。产物路径：

- `/mnt/d/Game/BeiDou/BeiDou-ijl15/out/Release/ijl15.dll`
- `/mnt/d/Game/BeiDou/BeiDou-ijl15/out/Release/ijl15.pdb`

当前已知 warning 如 `dllmain.cpp C4244` 和 `detours.pdb LNK4099` 不阻塞产物生成。

## 覆盖客户端

客户端加载的是 `/mnt/d/Game/BeiDou/BeiDou-Client/ijl15.dll`。覆盖前必须确认 `BeiDou.exe`
没有运行，并备份旧 DLL：

```bash
cp /mnt/d/Game/BeiDou/BeiDou-Client/ijl15.dll /mnt/d/Game/BeiDou/BeiDou-Client/ijl15.dll.codex-backup-$(date +%Y%m%d-%H%M%S)
cp /mnt/d/Game/BeiDou/BeiDou-ijl15/out/Release/ijl15.dll /mnt/d/Game/BeiDou/BeiDou-Client/ijl15.dll
cp /mnt/d/Game/BeiDou/BeiDou-ijl15/out/Release/ijl15.pdb /mnt/d/Game/BeiDou/BeiDou-Client/ijl15.pdb
```

PDB、备份 DLL、dump、`q-crash-watch.log` 和 `interaction-hook.log` 只用于本地诊断，默认不进入
客户端补丁提交。

## 崩溃诊断

排查 Q 窗口、任务点击或交互 Hook 崩溃时，优先使用诊断版 `ijl15.dll` 写
`interaction-hook.log`。需要系统 dump 时启用 Windows WER LocalDumps 到：

```text
D:\Game\BeiDou\BeiDou-Client\crash-dumps
```

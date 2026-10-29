# 分支 README 约定

本仓库的 `README.md` **按分支区分**：GitHub / GitLab 在浏览某个分支时，展示的是该分支里提交的 `README.md`。因此不需要额外工具，只要在不同分支上提交不同的 README 即可。

## 当前约定

| 分支 | 定位 | README 内容 |
| --- | --- | --- |
| `main` | YD-ESP32-S3 基础 MCP 节点 | 无屏节点、SoftAP 配网、GPIO/WS2812/OTA |
| `mcp-base` | MCP 基础固件 | 基础 MCP 服务端与网页配网 |
| `esp32-mcp-lcd43b` | 4.3" 桌面显示平台 | 4.3B、LVGL、屏上配网、显示工具、触摸待办 |

各分支 README 的顶部都会标注当前分支，并提示去其他分支查看对应版本。

## 修改某个分支的 README

```bash
git switch <branch>
# 编辑 README.md
git add README.md
git commit -m "docs: update branch README"
git push origin <branch>
```

只影响当前分支；其他分支的 README 不受影响。

## 分支策略

这些分支独立维护，`esp32-mcp-lcd43b` **不会合并回 `main`**，因此各分支的 README 可以长期保持不同，不需要考虑 README 合并冲突，也不需要配置 `merge=ours`。

如果以后需要把某个功能同步到其他分支，建议只 cherry-pick 代码和共享文档，跳过 `README.md`：

```bash
git cherry-pick -n <commit>
git restore --staged --worktree README.md
git commit
```

## 共享内容放哪里

README 只放“这个分支的入口信息和快速开始”。详细的、跨分支共享的文档放在：

- `docs/DESIGN.md`：基础 MCP 节点
- `docs/DISPLAY.md`：4.3B 显示平台、场景 schema、MCP 工具、P0 清单

这样每个分支的 README 可以保持简短，公共内容不会重复维护。

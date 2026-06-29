# Claude Code Slash Commands 速查

> 所有可用命令通过 `/` 触发，在对话中输入即可调用。

---

## 🎨 开发流程类

| 命令 | 功能说明 |
|---|---|
| `/brainstorming` | 创意/需求探讨 — 在实现新功能前探索用户意图、需求和设计 |
| `/writing-plans` | 编写实现计划 |
| `/executing-plans` | 在有书面计划后，在独立会话中执行并设置审查点 |
| `/subagent-driven-development` | 用多子代理并行执行计划中的独立任务 |
| `/test-driven-development` | 先写测试再实现的 TDD 流程 |
| `/verification-before-completion` | 完成前的验证检查 |
| `/finishing-a-development-branch` | 开发完成后决定合并、PR 还是清理 |

## 🔍 代码质量类

| 命令 | 功能说明 |
|---|---|
| `/code-review` | 审查当前 diff，找 bug 和可优化点（支持 `--comment` 和 `--fix`） |
| `/requesting-code-review` | 完成任务后主动请求代码审查 |
| `/receiving-code-review` | 收到审查反馈后，验证并实施修改建议 |
| `/systematic-debugging` | 遇到 bug 时系统化定位根因再修复，不直接盲猜改代码 |
| `/simplify` | 审查代码并自动应用复用、简化、效率优化 |

## 🔬 研究与安全类

| 命令 | 功能说明 |
|---|---|
| `/deep-research` | 多源联网研究 — 展开搜索、获取资料、对抗验证、输出带引用的深度报告 |
| `/security-review` | 安全审查 |
| `/review` | 通用审查 |

## 🛠 工具配置类

| 命令 | 功能说明 |
|---|---|
| `/update-config` | 配置 `settings.json`（权限、环境变量、钩子等） |
| `/keybindings-help` | 自定义键盘快捷键（和弦键、绑定修改等） |
| `/fewer-permission-prompts` | 扫描历史记录，将常用只读命令加入许可白名单以减少弹窗 |
| `/using-git-worktrees` | 创建 git worktree 隔离工作区 |

## 🚀 运行与验证类

| 命令 | 功能说明 |
|---|---|
| `/run` | 启动并查看项目运行效果 |
| `/verify` | 验证代码修改是否按预期工作 |
| `/loop` | 按间隔重复执行某个命令，如 `/loop 5m /foo`（默认 10m） |

## 🤖 其他

| 命令 | 功能说明 |
|---|---|
| `/claude-api` | Claude API / Anthropic SDK 参考（模型 ID、定价、参数、流式、工具调用、MCP 等） |
| `/using-superpowers` | 超级权限模式 |
| `/writing-skills` | 编写自定义 Skill |
| `/init` | 初始化 |
| `/dispatching-parallel-agents` | 派发多个独立任务并行执行 |

---

## 使用提示

- 大部分命令后可以跟参数，如 `/code-review --fix`
- `/loop` 适合轮询任务（如"每 5 分钟检查一次编译状态"）
- 组合使用效果更好：`/brainstorming` → `/writing-plans` → `/executing-plans` → `/verification-before-completion` → `/finishing-a-development-branch`

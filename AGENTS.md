# AGENTS.md

## 1. Project Purpose

本项目为 **Fast DDS** 开发一个基于 **OpenHarmony DSoftBus** 的自定义 Transport。

目标是在不改变 Fast DDS 上层 RTPS/DDS 语义的前提下，将 DSoftBus 作为底层数据传输能力，使 Fast DDS 能够利用 DSoftBus 提供的：

- 设备发现与分布式通信基础能力；
- 安全与鉴权能力；
- OpenHarmony 设备间通信能力；
- 高性能数据传输能力；

同时尽可能完整保留 Fast DDS 原生的：

- DDS / RTPS 语义；
- Participant / Endpoint discovery；
- QoS；
- Reliability；
- Fragmentation；
- History / Cache；
- Writer / Reader；
- RTPS message processing；

等上层能力。

核心原则：

> DSoftBus 只负责 Transport 所承担的底层传输职责。  
> Fast DDS 的 RTPS/DDS 行为不得依赖具体 Transport 实现。

---

## 2. Architectural Boundary

### 2.1 Transport boundary

本项目的主要修改范围必须限制在 Fast DDS Transport 层及其必要的适配代码。

除非有明确、经过论证且得到用户批准的架构原因，否则：

- 不修改 DDS 层语义；
- 不修改 RTPS 协议语义；
- 不修改 Writer / Reader 行为以迁就 DSoftBus；
- 不修改 discovery protocol 以迁就 DSoftBus；
- 不修改 Fast DDS QoS 语义；
- 不让上层代码感知 DSoftBus 特有实现。

必须维持如下抽象关系：

```text
DDS API
   ↓
Fast DDS
   ↓
RTPS
   ↓
Transport Interface
   ↓
DSoftBus Transport
   ↓
OpenHarmony DSoftBus Socket API
```

RTPS 层必须只依赖 Fast DDS Transport abstraction，而不能直接调用 DSoftBus API。

### 2.2 DSoftBus API restriction

只允许使用：

```text
DSoftBus Socket API
```

禁止使用：

```text
DSoftBus Session API
```

不得因为 Socket API 使用困难而退回 Session API。

---

## 3. Source of Truth

Agent 不得仅依据设计文档、TODO、计划或历史描述判断当前项目状态。

判断项目实际状态时，证据优先级为：

1. 当前适用的 `AGENTS.md`
2. 当前 Git working tree
3. 当前实际源代码
4. 实际 build / test 执行结果
5. RK3588A 实机测试结果
6. gate / status / run records
7. 当前项目文档
8. plans / TODO / historical notes

必须明确区分：

```text
PLANNED
IMPLEMENTED
BUILDS
HOST_TESTED
DEVICE_TESTED
INTEGRATION_TESTED
VERIFIED
BLOCKED
```

禁止因为代码已经存在就将功能标记为 `DONE` 或 `VERIFIED`。

---

## 4. DSoftBus Development Rules

在实现、修改或调试任何 DSoftBus 相关代码之前，必须优先检查本仓库中的：

- `communication_dsoftbus` 官方文档；
- API 示例；
- Socket 相关测试代码；
- 权限配置；
- system ability / bundle / permission requirements；
- OpenHarmony 设备部署要求。

特别关注：

- permission；
- capability；
- socket lifecycle；
- bind / listen / connect；
- shutdown；
- peer identification；
- asynchronous callback；
- buffer ownership；
- thread safety；
- failure handling；
- device networking requirements。

禁止在没有检查 DSoftBus 官方示例和测试代码的情况下，根据 API 名称猜测其行为。

如果测试失败，应首先排查：

```text
permission
configuration
device state
network state
DSoftBus lifecycle
API contract
```

而不是立即修改 Fast DDS RTPS 逻辑。

---

## 5. Implementation Rules

### 5.1 Preserve Fast DDS semantics

实现 DSoftBus Transport 时，应尽可能复用 Fast DDS 已有 Transport architecture 和生命周期模型。

不得通过修改上层协议行为来掩盖 Transport 层问题。

例如，禁止为了使测试通过而：

- 绕过 RTPS message processing；
- 修改 discovery 行为；
- 修改 reliability semantics；
- 修改 QoS 语义；
- 在上层加入 DSoftBus-specific branching；
- 将 Transport failure 伪装成正常 RTPS 行为。

### 5.2 Minimal change principle

优先选择：

```text
最小 Fast DDS 修改
+
独立 DSoftBus Transport 实现
```

而不是：

```text
大量修改 Fast DDS 内部逻辑
+
少量 DSoftBus adapter
```

如果必须修改 Transport abstraction 本身，需要首先证明：

1. 当前 abstraction 无法表达所需行为；
2. 修改对其他 Transport 不产生语义破坏；
3. 修改不是为了绕过 DSoftBus 自身限制。

### 5.3 No silent workaround

不得通过以下方式静默绕过失败：

- 删除断言；
- 禁用测试；
- 降低验证范围；
- 将 device test 替换成 host test；
- 无限增加 timeout；
- 忽略返回码；
- 忽略错误 callback；
- 修改预期结果以匹配错误实现。

任何 workaround 都必须显式记录。

---

## 6. Testing Policy

本项目采用 **test-driven and evidence-driven development**。

实现 Agent 不负责自行宣布实现正确。

必须使用独立 `tester` subagent 对当前实际 working tree 进行验证。

基本原则：

> Implementation produces code.  
> Tester produces execution evidence.

Tester 应优先执行最小有效测试，然后逐步扩大验证范围：

```text
targeted test
    ↓
component test
    ↓
Fast DDS integration test
    ↓
DSoftBus integration test
    ↓
RK3588A device test
    ↓
cross-device / regression test
```

测试范围应与修改影响范围相匹配。

共享、底层或关键 Transport 行为发生变化时，必须扩大 regression scope。

---

## 7. RK3588A Device Verification

涉及 DSoftBus Transport 实际通信行为的功能，host-only test 不足以证明完成。

必须使用：

```text
$hdc
```

控制 RK3588A OpenHarmony 开发板完成实机验证。

至少记录：

- build artifact；
- deployment target；
- device；
- executed command；
- exit code；
- relevant logs；
- PASS / FAIL / BLOCKED。

如果功能涉及两个 OpenHarmony 节点之间的通信，则应进行真实跨设备测试。

设备不可用时：

```text
DEVICE_TESTED = false
```

必须报告 `BLOCKED` 或相应的未验证状态。

禁止将 host test 的成功解释为 device test 成功。

---

## 8. Independent Review

代码实现并通过第一轮 tester 验证后，必须运行：

```text
$agentic_review
```

审核至少关注：

- correctness；
- Fast DDS architectural boundary；
- Transport abstraction；
- concurrency；
- lifecycle；
- resource ownership；
- error handling；
- memory safety；
- DSoftBus API contract；
- regression risk；
- unnecessary complexity。

Review 发现的问题由 implementation agent 修复。

`agentic_review` 自身不得代替实际测试。

---

## 9. Documentation Policy

项目文档必须反映 **当前已经得到证据支持的实际状态**，而不是预期状态。

在最终验证完成后、Git commit 之前，必须派遣：

```text
docs_updater
```

`docs_updater` 应依据：

- 当前 Git diff；
- 实际源代码；
- tester results；
- device results；
- review 后重新执行的测试；
- gate/status records；

更新项目文档。

禁止出现：

```text
implemented → automatically documented as DONE
```

应该使用例如：

```text
Implementation: PRESENT
Build: PASS
Host Test: PASS
RK3588A Test: PASS
Cross-device Test: BLOCKED
Overall: NOT VERIFIED
```

---

## 10. Required Validation Workflow

涉及实现代码的正常开发任务必须按照以下顺序执行：

```text
IMPLEMENT
    ↓
TEST
    ↓
AGENTIC REVIEW
    ↓
FIX REVIEW FINDINGS
    ↓
RE-TEST
    ↓
DOCUMENTATION SYNC
    ↓
PRE-COMMIT GATE
    ↓
COMMIT
```

这些步骤是顺序执行的。

不得并发运行会依赖彼此结果的阶段。

---

## 11. Detailed Pre-Commit Workflow

### Step 1 — Implementation

Implementation agent 完成预定代码修改。

Implementation agent 不得仅根据：

- code inspection；
- compilation；
- reasoning；

宣称功能已经验证完成。

---

### Step 2 — Initial Testing

Spawn：

```text
tester
```

Tester 必须验证当前实际 working tree。

Tester result 是当前代码执行状态的 authoritative evidence。

如果 tester 报告：

```text
FAIL
BLOCKED
```

则返回 implementation/debugging 阶段。

不得继续将任务描述为完成。

---

### Step 3 — Independent Review

初始测试通过后运行：

```text
$agentic_review
```

处理所有需要修复的问题。

如果 review 只提出非阻塞建议，可以明确记录而不强制实施。

如果 review 发现 correctness、architecture、safety 或 regression 问题，则必须修复。

---

### Step 4 — Final Testing

所有 review-driven modifications 完成之后，再次 spawn：

```text
tester
```

必须重新执行受影响测试。

不得使用 review 前的测试结果证明 review 后代码仍然正确。

最终 tester result 是 commit 前的 canonical execution evidence。

---

### Step 5 — Documentation Sync

最终 tester 完成后 spawn：

```text
docs_updater
```

向其提供最终测试结果。

`docs_updater` 必须检查：

- current working tree；
- Git status；
- staged / unstaged diff；
- actual implementation；
- relevant documentation；
- final tester evidence；
- RK3588A/device evidence；
- relevant gate records。

Documentation 必须描述实际验证状态。

---

### Step 6 — PRE_COMMIT Gate

执行 `git commit` 前，Main / Orchestrator 必须确认：

1. implementation 已完成；
2. initial tester 已执行；
3. `$agentic_review` 已执行；
4. review 的阻塞问题已经处理；
5. review 后 final tester 已执行；
6. final tester 报告 `Commit gate: PASS`；
7. `docs_updater` 已完成；
8. `docs_updater` 报告 `Ready for commit: YES`；
9. RK3588A/device verification 满足当前任务要求；
10. 没有意外修改 tracked files；
11. 当前 documentation 与实际代码和验证证据一致；
12. applicable project gates 允许提交。

只有上述条件全部满足，正常 implementation commit 才允许执行。

---

## 12. Failure and Blocked-State Policy

如果任何必要验证无法完成，不得伪造成功状态。

使用：

```text
PASS
FAIL
BLOCKED
SKIPPED
```

并说明原因。

尤其：

```text
SKIPPED != PASS
BLOCKED != PASS
IMPLEMENTED != VERIFIED
BUILDS != TESTED
HOST_TESTED != DEVICE_TESTED
```

遇到失败时：

1. 保留失败证据；
2. 确认实际 failing command；
3. 记录 exit code；
4. 记录必要日志；
5. 判断失败属于 implementation、environment 或 infrastructure；
6. 再进入对应修复流程。

不得为了维持进度状态而降低 gate 标准。

---

## 13. Subagent Responsibilities

### tester

负责：

- build；
- test execution；
- integration verification；
- RK3588A/device test；
- execution evidence。

不得：

- 修改 production implementation；
- 修改文档；
- stage；
- commit；
- amend；
- push。

### docs_updater

负责：

- 检查项目真实状态；
- 同步 project documentation；
- 更新 status/gate/progress documentation。

不得：

- 修改 production implementation；
- 修改测试使其通过；
- stage；
- commit；
- amend；
- push。

---

## 14. Git Ownership

只有 Main / Orchestrator 可以执行：

```text
git add
git commit
git commit --amend
git push
```

Subagent 不拥有 Git state transition 权限。

正常情况下禁止在验证完成之前 commit。

Checkpoint / diagnostic commit 只有在项目 workflow 明确允许时才可创建。

此类 commit 必须明确记录：

```text
FAIL
BLOCKED
UNVERIFIED
```

状态，不得伪装为正常完成提交。

---

## 15. Definition of Done

涉及 DSoftBus Transport 的功能只有同时满足适用条件时才能被标记为完成：

```text
[ ] implementation present
[ ] build successful
[ ] targeted tests pass
[ ] Fast DDS integration tests pass
[ ] DSoftBus integration tests pass
[ ] required RK3588A tests pass
[ ] required cross-device tests pass
[ ] agentic review completed
[ ] review fixes re-tested
[ ] documentation synchronized
[ ] no unresolved blocking regression
[ ] PRE_COMMIT gate PASS
```

不适用于当前修改的验证项目可以标记为 `N/A`，但必须说明理由。

不得把未经执行的项目标记为 PASS。

<!-- gitnexus:start -->
# GitNexus — Code Intelligence

This project is indexed by GitNexus as **FastDDS-dsoftbus-Transport** (170086 symbols, 282759 relationships, 498 execution flows).

> Index stale? Run `node .gitnexus/run.cjs analyze --index-only` from the project root — it auto-selects an available runner. No `.gitnexus/run.cjs` yet? Bootstrap with `npx`, `bunx`, or `pnpm dlx` — e.g. `bunx gitnexus@latest analyze` (npm 11 npx crash; #1939).

## Always Do

- **MUST run impact before editing.** Use `impact({target: "symbolName", direction: "upstream"})` or `node .gitnexus/run.cjs impact "symbolName" --direction upstream --repo .`; report callers, processes, and risk. Never substitute grep for graph analysis.
- **MUST analyze graph changes before committing.** Use `detect_changes({scope: "all"})` (MCP) or `node .gitnexus/run.cjs detect-changes --scope all --repo .` (CLI fallback). `partial: true` or `truncated: true` is not a clean check — a zero means unseen, not unaffected; re-run it. For regression review: `detect_changes({scope: "compare", base_ref: "main"})` or `node .gitnexus/run.cjs detect-changes --scope compare --base-ref "main" --repo .`.
- MUST warn on HIGH/CRITICAL `risk` pre-edit; never use `riskSharedAxes` to waive a HIGH/CRITICAL `risk` warning. Compare File/symbol: MCP File omits axes; Graph-RAG expands File.
- **MUST treat `risk: UNKNOWN` as unresolved, not as low.** An empty caller set is not evidence the symbol is unused — it can also mean the callers are not resolvable by the index (plain-object property access, dynamic dispatch, cross-language calls). `impact` pairs `UNKNOWN` with a `riskNote` saying so. Confirm with a text search before treating the symbol as safe to change or delete; do not proceed on the strength of a zero.
- **MUST use `query({search_query: "concept"})` for concepts/flows, `context({name: "symbolName"})` for a named symbol, or `impact` for blast radius, on read-only callers, dependencies, imports, or execution flow.** Graph first; text search only for empty/`UNKNOWN`/literals.
- For security review, `explain({target: "fileOrSymbol"})` lists taint findings (source→sink flows; needs `analyze --pdg`).

## Never Do

- NEVER edit a function, class, or method before MCP/CLI impact analysis.
- NEVER ignore HIGH or CRITICAL risk warnings from impact analysis, and never read `UNKNOWN` as an all-clear — it means the walk could not answer, which is the one verdict that requires confirming by other means.
- NEVER rename symbols with find-and-replace — use `rename` which understands the call graph.
- NEVER commit before MCP/CLI graph change analysis.

## Resources

| Resource | Use for |
| --- | --- |
| `gitnexus://repo/FastDDS-dsoftbus-Transport/context` | Codebase overview, check index freshness |
| `gitnexus://repo/FastDDS-dsoftbus-Transport/clusters` | All functional areas |
| `gitnexus://repo/FastDDS-dsoftbus-Transport/processes` | All execution flows |
| `gitnexus://repo/FastDDS-dsoftbus-Transport/process/{name}` | Step-by-step execution trace |

## CLI

| Task | Read this skill file |
| --- | --- |
| Understand architecture / "How does X work?" | `.claude/skills/gitnexus-exploring/SKILL.md` |
| Blast radius / "What breaks if I change X?" | `.claude/skills/gitnexus-impact-analysis/SKILL.md` |
| Trace bugs / "Why is X failing?" | `.claude/skills/gitnexus-debugging/SKILL.md` |
| Rename / extract / split / refactor | `.claude/skills/gitnexus-refactoring/SKILL.md` |
| Tools, resources, schema reference | `.claude/skills/gitnexus-guide/SKILL.md` |
| Index, status, clean, wiki CLI commands | `.claude/skills/gitnexus-cli/SKILL.md` |

<!-- gitnexus:end -->

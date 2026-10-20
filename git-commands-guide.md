# Git 命令学习记录

这份文档记录 Claude 在项目中**实际使用过**的 Git 命令，并解释它们的作用。以后使用新的命令或选项时继续补充；只读命令不会改变仓库，写入命令则会注明影响范围。记录中示例使用 `bbApp` 子仓库，路径应按实际项目调整。不会记录 Git 命令输出中可能包含的密码、令牌等敏感内容。

## 先理解两个仓库

当前项目 `project_common` 是主仓库；`bbApp` 是 Git 子模块，即拥有自己提交记录的独立仓库。`git -C bbApp ...` 表示让 Git 在 `bbApp` 目录执行命令，**不是**让主仓库提交 `bbApp` 内的文件。在子模块内提交后，主仓库的 `bbApp` 子模块指针仍可能显示为修改，需要明确决定是否在主仓库另行提交。

## 查看状态和历史（只读）

| 用过的命令示例 | 含义 |
| --- | --- |
| `git status --short` | 简略显示工作区状态。` M` 表示已跟踪文件有未暂存的修改，`??` 表示未跟踪文件，` D` 表示未暂存的删除；子模块状态也会显示在主仓库中。 |
| `git -C bbApp status --short --branch` | 在子仓库中同时查看当前分支或 `detached HEAD` 状态以及文件改动。`detached HEAD` 表示当前指向一个提交，不在普通分支上；这时新提交不会自动推进某个分支。 |
| `git status --short -- <路径>` | 只查看指定路径的状态；`--` 将路径与 Git 的选项或修订名分开。 |
| `git -C bbApp status --short --untracked-files=all` | 展开列出未跟踪目录中的具体文件，而不是只显示目录名。 |
| `git log -1 --format='%h %ad %s' --date=iso -- <文件>` | 查指定文件最近一次相关提交：短提交号、提交时间、标题。`-1` 只取最近一条。 |
| `git -C bbApp log -12 --date=format:'%Y-%m-%d %H:%M:%S %z' --format='%h | %ad | %an | %s' -- Makefile` | 查子仓库中 `Makefile` 最近 12 条提交，按指定格式展示提交号、日期、作者、标题。 |
| `git -C bbApp log --oneline -8 -- Makefile` | 简要列出该文件最近 8 条提交；`--oneline` 一条提交占一行。 |
| `git log --all --author='liuzhixiang' --since='2026-10-09 00:00:00 +0800' --until='2026-10-16 23:59:59 +0800' --format='%h %ad %s' --date=short` | 按作者和时间区间查询提交；`--all` 纳入本地可见的所有分支和标签，`--author` 按作者标识匹配，`--since`/`--until` 指定时间上下限，`--format` 定制输出。它只查当前仓库，子模块需分别查询；`--all` 也不能保证包含尚未从远端取得的提交。 |
| `git -C bbApp log -1 --format='%H%n%P%n%s'` | 查看完整提交号、父提交号和标题；`%n` 是换行。 |
| `git -C bbApp rev-parse --short HEAD` | 取当前提交 `HEAD` 的短哈希值。 |
| `git -C bbApp branch -r --contains HEAD` | 查看当前提交被哪些**本地已知的远程跟踪分支**包含；结果为空不能单独证明远端服务器绝对没有该提交，可能需要先更新远程跟踪信息。 |
| `git log --all --since='2026-10-09 00:00:00 +0800'` | 查看当前仓库本地所有可见引用中的最近提交；`--all` 可能包含 stash 引用，统计正式提交时要剔除 stash 伪提交。子模块是独立仓库，需分别查询。 |
| `git show --stat --first-parent <合并提交>` | 沿合并提交的第一父提交观察这次合并带来的文件变化；它不代表新增内容均由合并者原创。 |
| `git stash list`、`git stash show` | 查看当前仓库临时保存的改动列表和某项的内容摘要；stash 不等于正式功能提交，子模块的 stash 也只存在于该子模块。 |
| `git -C bbApp remote -v` | 查看远程仓库名称及 fetch/push 地址；不要在分享输出时泄露含凭据的地址。 |
| `git ls-files <路径>` | 列出 Git 跟踪的文件；结合 `wc -l` 可统计数量。 |

## 查看差异和提交内容（只读）

| 用过的命令示例 | 含义 |
| --- | --- |
| `git diff -- <路径>` | 查看指定路径**未暂存**的修改，不会显示未跟踪文件。 |
| `git -C bbApp diff --cached -- Makefile` | 查看已暂存、准备进入下一次提交的 `Makefile` 差异；`--cached` 也可写作 `--staged`。 |
| `git diff --stat -- <路径>` | 只显示差异统计，方便了解改动规模；二进制文件会标注 `Bin`。 |
| `git diff --name-only -- <路径>` | 仅列出有差异的文件名。 |
| `git diff --numstat -- <路径>` | 显示每个文件增加和删除的行数；二进制文件一般显示 `-`。 |
| `git -C bbApp diff HEAD -- Makefile` | 比较工作区的 `Makefile` 与当前提交；用于检查文件是否有未提交修改。 |
| `git -C bbApp diff 9cde668cb^ HEAD -- Makefile` | 比较指定提交的父版本与当前提交的 `Makefile`，判断后续提交是否再次改动它。`^` 表示第一个父提交。 |
| `git diff --submodule=short -- bbApp` | 显示子模块指针变化的提交号；应在包含该子模块的主仓库运行。 |
| `git -C bbApp show --stat --oneline <提交>` | 看一次提交的标题和文件变化统计。 |
| `git -C bbApp show --format= --no-ext-diff 9cde668cb -- Makefile` | 仅显示指定提交对 `Makefile` 的补丁；`--format=` 隐去提交消息，`--no-ext-diff` 禁用外部差异工具。 |
| `git -C bbApp show 9cde668cb^:Makefile` | 从某个历史版本读取文件内容；`提交^:路径` 中的路径相对于指定仓库。它本身只输出内容，不会修改文件。 |
| `git -C bbApp show --format= --name-status HEAD` | 查看当前提交涉及的文件及状态，如 `M` 修改、`A` 新增、`D` 删除。 |
| `git -C bbApp -c core.whitespace=cr-at-eol diff --check` | 检查差异中新增行的空白问题。`-c` 仅本次临时设置 Git 参数；`cr-at-eol` 将旧式 `CRLF` 行尾的回车视为正常，避免误报。 |
| `git -C bbApp diff --cached --name-status` | 列出暂存区中拟提交文件的增删改状态。 |
| `git -C bbApp diff --cached --stat` | 只查看子仓库**暂存区**的文件与增删行数，用于提交前核对范围；不会显示未暂存或未跟踪文件的内容。 |
| `git -C bbApp diff --cached --check` | 检查子仓库**暂存区**新增行的空白错误；不会修改文件，也不检查未暂存内容。若涉及旧式 CRLF 行尾，需按文件实际编码判断报告。 |
| `git -C bbApp diff --check` | 检查子仓库**未暂存**差异的空白错误；与 `--cached --check` 检查的范围不同，两者都不会修改文件。 |

## 暂存和提交（会改变 Git 状态）

| 用过的命令示例 | 含义与注意事项 |
| --- | --- |
| `git -C bbApp add -- src_MultiCenter/BbClient808.cpp src_MultiCenter/BbClient808B.cpp tests/test_zero_distance_report.py` | **暂存**这几个明确列出的文件，准备提交；不等于已提交。提交前应运行 `git diff --cached --stat` 检查暂存范围。此前曾误将测试文件暂存并提交，随后按要求修正。 |
| `git -C bbApp commit -m '标题' -m '正文' -m 'Co-Authored-By: ...'` | 把**暂存区**内容制成一个新提交；每个 `-m` 是一段消息。不会自动包含其他未暂存、未跟踪文件，也不会自动推送到远端。 |

本次先用 `git -C bbApp add -- src_MultiCenter/BbTlsClient.cpp tests/test_tls_cert_validity.py tests/test_tls_diagnostics.py tests/test_tls_keylog.py` 精确暂存四个文件，避免把子模块中其他潜在改动带入提交；`--` 后的路径相对于 `bbApp` 子仓库。提交完成后，主仓库仍只显示 `bbApp` 子模块指针修改；**本次未提交主仓库指针，也未推送**。
| `git -C bbApp rm --cached -- tests/test_zero_distance_report.py` | 只把文件从 Git **索引/暂存区**移除，保留磁盘上的文件。如果文件已在旧提交中，下一次提交会记录“删除该文件”；配合 `commit --amend` 可使本地最新提交不再包含它。不要误用没有 `--cached` 的 `git rm`，那会删除工作区文件。 |
| `git -C bbApp commit --amend --no-edit` | 用当前暂存内容替换最近一次提交，同时沿用原提交消息；会生成**新的提交号**。只适合确认没有被别人依赖的本地提交；若已经推送，不能擅自改写公共历史。 |
| `git -C bbApp commit --amend -m '标题' -m '正文' ...` | 修改最近一次提交内容或消息，也会改变提交号。本次用它修正错误的提交说明。 |

## 恢复文件（会改变工作区，先确认目标）

本次在确认 `Makefile` 没有后续修改后，用了以下命令：

```sh
git -C bbApp show 9cde668cb^:Makefile > bbApp/Makefile
```

左侧的 `git show` **只读**历史文件；右侧的 `>` 是 Shell 重定向，会**覆盖当前 `bbApp/Makefile`**。其效果是将这个文件恢复到 `9cde668cb` 之前的内容，作为**未提交的工作区改动**；既没有撤回该提交的其他文件，也没有删除历史提交。执行前必须先确认当前文件没有需要保留的未提交修改，并核对目标提交之后是否还有其他改动。之后使用 `git diff -- Makefile` 和 `cmp` 核对恢复结果。

## 获取与合并主干（会改变 Git 状态）

| 本次使用的命令 | 含义与注意事项 |
| --- | --- |
| `git fetch origin project_merge` | 从 `origin` 获取 `project_merge` 的最新提交，更新远程跟踪信息，**不会**合并到当前分支，也不会覆盖工作区改动。 |
| `git status --short --branch`、`git branch -vv` | 分别查看工作区/当前分支，以及本地分支所跟踪的远程分支和领先/落后状态；例如 `[ahead 13, behind 2]` 表示双方各有独有提交。 |
| `git symbolic-ref refs/remotes/origin/HEAD` | 查看 `origin` 标记的默认分支（本项目是 `origin/project_merge`）；不同项目的主干名称可能不同。 |
| `git log --oneline --left-right --cherry-pick HEAD...origin/project_merge` | 列出分支两侧独有的提交，`<` 为当前分支、`>` 为主干；`--cherry-pick` 尝试忽略补丁等价的提交。`...` 使用两边共同祖先作为比较起点。 |
| `git merge-base HEAD origin/project_merge` | 找两条分支的最佳共同祖先，供比较和预检使用。 |
| `git merge-tree <共同祖先> HEAD origin/project_merge` | 在不更改工作区的情况下模拟三方合并，检查可能的冲突。本机 Git 2.25.1 不支持新版本的 `merge-tree --write-tree`，因此改用这种写法。 |
| `git merge --no-commit --no-ff origin/project_merge` | 把主干内容合入当前分支，但先不生成提交，便于检查冲突和差异；进入合并中的状态，之后必须完成合并提交或按需中止。`--no-ff` 要求产生合并提交，即使快进也不直接移动指针。执行前应先核查未提交改动是否会被覆盖。 |
| `git diff --name-only --diff-filter=U` | 只列出合并中存在冲突、尚未解决的文件；输出为空代表没有未解决的冲突。 |
| `git merge-base --is-ancestor origin/project_merge HEAD` | 退出码为 0 表示主干当前提交已经是本分支 `HEAD` 的祖先，即合并已被当前分支包含。 |
| `git commit -m '合并标题' -m '说明' ...` | 在检查完无冲突的合并结果后创建合并提交，保留两条历史的关系；只提交合并的暂存内容，不会自动包含其他未暂存改动，也不会自动推送。 |

本次从 `origin/project_merge` 合并到功能分支的提交是 `deba55b1c`，没有执行 `git push`。主仓库现有未提交改动及 `bbApp` 等子模块的独立工作区改动仍然保留。

## 子模块远端更新（本次只获取，尚未移动子模块 HEAD）

| 本次使用的命令 | 含义与注意事项 |
| --- | --- |
| `git submodule status` | 列出各子模块当前检出提交；前缀 `+` 表示当前提交与主仓库记录的指针不同，前缀 `-` 表示子模块尚未初始化。工作区内未提交的文件修改需进一步用子模块自己的 `git status` 查看。 |
| `git submodule foreach --recursive 'git fetch origin'` | 进入每个已初始化子模块执行 `git fetch origin`，更新本地的远端跟踪信息（如 `origin/main`），**不移动子模块的 HEAD，也不会修改工作区文件**；`--recursive` 连嵌套子模块也执行。 |
| `git rev-list --left-right --count HEAD...origin/main` | 在某个子模块中统计相对于 `origin/main` 双方各自独有的提交数量：第一个数字属于当前 `HEAD`，第二个属于远端 `main`。两个数字都非零，表示两边历史已分叉，不能简单快进。 |
| `git -C <子模块目录> status --short --branch` | 查看单个子模块的当前分支或游离 `HEAD` 状态及本地修改，不能用主仓库的 `git status` 代替。 |

获取远端提交不等于将它们合并或检出到子模块：是否升级子模块到远端 `main`，需要结合本地提交与未提交改动决定；切勿对有改动的子模块直接执行覆盖式更新。

### 本次进一步更新到各子模块的 `origin/main`

| 命令示例 | 含义与注意事项 |
| --- | --- |
| `git -C GuiQT merge --ff-only origin/main` | 只允许**快进**到 `origin/main`；不会创建合并提交，存在分叉则拒绝。`GuiQT`、`onvif_gsoap_lib` 用了这一方式。 |
| `git -C bbApp switch -c feature/local-808-updates` | 从当前提交新建本地分支并切换过去，保护 `detached HEAD` 上的本地提交；不会推送远端。类似操作也用于 `serverlib` 和 `mdvrsdk`。 |
| `git -C bbApp merge --no-edit origin/main` | 把远端 `main` 合入当前子模块分支；双方有独立提交时产生**合并提交**。`--no-edit` 沿用 Git 自动生成的合并说明。`bbApp`、`serverlib` 已这样合并，本地文件改动仍保留。 |
| `git -C libsource/mdvrsdk stash push -m '说明' -- include/bbParamdef.h` | 临时存放这个文件的未提交修改，以便先更新子模块；stash 是当前仓库的**本地临时记录**，不会自动推送。操作前后要检查 `git stash list`，防止遗漏。`bbCfgCmn` 已与 `origin/main` 同提交但有 4 行未提交改动时，也用 `stash push -m '说明' -- src/bbCfgCmnOp.cpp` 备份并清理工作区，使其使用远端文件版本。 |
| `git -C libsource/mdvrsdk stash apply stash@{0}` | 将最近一份 stash 应用回工作区，**不会自动删除 stash**；若与新版本修改同一区域会出现冲突，需要人工核对并解决。本次双方添加同名字段，保留一个字段及本地注释；stash 暂保留作为备份。 |
| `git -C libsource/mdvrsdk reset -- include/bbParamdef.h` | 把指定文件从**暂存区**移回未暂存状态，**不删除磁盘文件，也不回退文件内容**。不带路径的 `reset` 或加 `--hard` 效果不同，不能混用。 |
| `git -C libsource/mdvrsdk restore --source=origin/main --worktree -- include/bbParamdef.h` | **覆盖工作区中这个指定文件**，使其与远端跟踪版本相同；不会改动提交、其他文件或暂存区。执行前先用 `git diff -- 文件` 核对将被丢弃的本地改动。本次用户明确选择以远端版本为准，原 stash 仍留作备份。 |
| `git -C libsource/mdvrsdk diff --exit-code origin/main -- include/bbParamdef.h` | 比较文件与远端版本是否完全一致：无差异时退出码为 0，有差异时为非 0，适合在恢复后核验。 |
| `git -C libsource/mdvrsdk diff --name-only --diff-filter=U` | 列出未解决冲突的文件；本次在人工处理冲突后，输出为空。 |

这次更新后的子模块 `HEAD` 已包含各自的 `origin/main`。主仓库只看到**子模块指针变动（未提交）**；其指针和这些子模块内的新合并提交仍需分别决定是否提交与推送。

## 关于“提交”和“推送”

`git commit` 只把变更记录在本地当前仓库；`git push` 才会把提交发送到远程仓库。本会话对上述修复执行了 `commit` 和 `commit --amend`，**没有执行 `git push`**。在子模块里提交源代码，也不等于自动提交主仓库的子模块指针。

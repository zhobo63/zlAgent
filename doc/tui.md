# TUI

`TUI` 是项目的终端输出层（`include/tui.h` / `src/tui.cpp`），提供两种输出方式：

| 接口 | 风格 | 目标流 | 说明 |
|------|------|--------|------|
| `TUI::cout` | 流式 `operator<<` | `std::cout` | 支持 `std::string` / `int`，链式调用，`flush()` 手动刷新 |
| `TUI::cerr` | 流式 `operator<<` | `std::cerr` | 错误输出（logger 的 Warn/Error 级别走此流） |
| `TUI::out(fmt, ...)` | printf 风格 | `std::cout` | 线程安全（互斥锁），自动 flush，并触发 `agent::send_event("out", ...)` |
| `TUI::err(fmt, ...)` | printf 风格 | `std::cerr` | 线程安全，自动 flush（当前无调用方） |
| `TUI::set_output_enabled(bool)` | — | — | 全局开关，默认 `true`，关闭后 `out`/`err`/`printDim` 全部静默 |

> 注意：`TUI::cout` / `TUI::cerr` 的 `operator<<` 目前**不检查** `s_enabled`，也不加锁；
> 而 `TUI::out()` 会检查 `s_enabled`、加锁并广播事件（供远程/客户端接收输出）。

---

## 一、TUI::cout 使用点（流式输出）

### 1. `include/logger.h` — 日志系统
| 行号 | 代码 | 说明 |
|------|------|------|
| 87 | `auto& stream = use_stderr ? TUI::cerr : TUI::cout;` | `detail::emit()` 中：Warn/Error 走 `TUI::cerr`，Debug/Info 走 `TUI::cout`，输出 `[级别] [组件] 消息` 并着色 |

### 2. `src/main.cpp` — 主程序
| 行号 | 代码 | 说明 |
|------|------|------|
| 92 | `TUI::cout << bar.str();` | 打印状态栏（回复模式图标、Msg/Fact 计数、SafetyGuard 状态） |
| 288 | `TUI::cout << "\n";` | Ctrl-C 后补换行（KeyWatcher 回调内） |
| 294 | `TUI::cout << "\n";` | readline 结束后补换行 |

### 3. `src/key_watcher.cpp` — 行编辑器（LineBuffer）
| 行号 | 代码 | 说明 |
|------|------|------|
| 446-448 | `TUI::cout << TUI::cursor_pos(prompt_row, 1) << TUI::ANSI_CLEAR_TO_END << ...` | `clear_prompt()`：定位光标并清除整行 |
| 459-461 | `TUI::cout << TUI::cursor_pos(prompt_row + draw_row - 1, draw_col) << TUI::ANSI_CLEAR_TO_END << ...` | `clear()`：清除已绘制的输入文本 |
| 467 | `TUI::cout << prompt;` | `draw()`：首次绘制提示符 |
| 476 | `TUI::cout << draw_text;` | `draw()`：增量绘制输入文本 |
| 645 | `TUI::cout << cmd;` | 菜单打开前滚动屏幕（输出 N 个换行） |

### 4. `src/command_handlers.cpp` — 斜杠命令
| 行号 | 代码 | 说明 |
|------|------|------|
| 660 | `TUI::cout << out.str();` | `/scan` 命令：打印项目扫描报告 |

### 5. `src/tool.cpp` — 工具参数展示
| 行号 | 代码 | 说明 |
|------|------|------|
| 18, 20 | `TUI::cout << indent << it.key() << ": ";` / `TUI::cout << '\n';` | `show_json()`：对象键值对 |
| 28 | `TUI::cout << '"' << s << '"';` | 字符串值 |
| 33 | `TUI::cout << '[' << size << " items]\n";` | 数组大小 |
| 39, 41, 43 | `TUI::cout << indent << "- ";` 等 | 数组元素缩进列表 |
| 47 | `TUI::cout << args.dump();` | 数字值 |
| 50 | `TUI::cout << (args.get<bool>() ? "true" : "false");` | 布尔值 |
| 53 | `TUI::cout << "null";` | null 值 |
| 59 | `TUI::cout << args << '\n';` | `show_text()`：纯文本 |
| 63, 72 | `TUI::cout << TUI::ANSI_BRIGHT_BLACK;` / `TUI::cout << TUI::ANSI_RESET;` | `show_json_text()`：灰色包裹 JSON 参数展示 |

### 6. `src/user_reply.cpp` — 用户回复模式
| 行号 | 代码 | 说明 |
|------|------|------|
| 68 | `TUI::cout << u8"\n\u23F8  [User Reply]";` | 干预提示头 |
| 70, 72 | `TUI::cout << " Tool failed: " << tool_name;` / `" Tool: "` | 工具名（失败/预检） |
| 74-75 | `TUI::cout << "\n" << "    Args: " << json_args << "\n";` | 参数展示 |
| 81 | `TUI::cout << "    Error: " << truncate(err_preview, 120) << "\n";` | 错误首行预览 |
| 85-88 | `TUI::cout << "\n" << "    What would you like to do?\n" ...` | 操作说明（y 重试 / n 跳过） |
| 90 | `TUI::cout << "\n    Reply: ";` | 等待输入提示 |

### 7. `src/terminal_command_detector.cpp` — 终端命令检测
| 行号 | 代码 | 说明 |
|------|------|------|
| 239 | `TUI::cout << buffer;` | `execute_directly()`：逐块打印命令输出（popen） |
| 262-264 | `TUI::cout << u8"\u26A0 Detected possible terminal command: " << input << "\n" << u8"   Execute directly? [y/N]: ";` | 低置信度命令确认提示 |

### 8. `src/project_summary/summary_tool.cpp` — 项目摘要
| 行号 | 代码 | 说明 |
|------|------|------|
| 594 | `TUI::cout << u8"=== 專案摘要 ===\n\n";` | `print_preview()` 标题 |
| 599-603 | `TUI::cout << u8"📁 " << name << ...` | 每个模块的文件/行数/类/函数统计 |
| 606-610 | `TUI::cout << u8"\n📊 Total: " << ...` | 总计行 |

### 9. `tools/file_tool.cpp` — read_files 工具
| 行号 | 代码 | 说明 |
|------|------|------|
| 163-166 | `TUI::cout << "  paths: " << count << " file(s)\n";` 等 | `show_arguments()`：paths 模式 |
| 173-188 | `TUI::cout << "  files: " << count << " file(s)\n";` 等 | files 模式（含行范围、[outline] 标记） |
| 197-200 | `TUI::cout << "  directory: '" << dir << "'";` 等 | directory + glob 模式 |
| 206 | `TUI::cout << "  outline: true\n";` | 顶层 outline 标志 |
| 1051, 1053 | `TUI::cerr << "show_arguments error: " << e.what() << '\n';` | 异常走 `TUI::cerr` |
| 1082, 1084 | `TUI::cerr << "show_preview error: " << e.what() << '\n';` | 异常走 `TUI::cerr` |

---

## 二、TUI::out 使用点（printf 风格）

### 1. `src/main.cpp` — 主程序
| 行号 | 代码 | 说明 |
|------|------|------|
| 105 | `TUI::out(u8"\nGoodbye!\n");` | 退出前告别语 |
| 131 | `TUI::out("\nAgent: ");` | 响应前缀 |
| 149 | `TUI::out("%s", token.c_str());` | LLM 流式 token 逐段输出 |
| 162-167 | `TUI::out(u8"\n\n⏱  Tokens: ");` 等 | Token 用量统计（prompt/completion/total） |
| 187 | `TUI::out("Usage: zlagent [options]\n" ...);` | 命令行帮助 |
| 203-205 | `TUI::out(u8"╭────...╮\n");` 等 | 启动横幅（ZL Agent - Code Assistant） |
| 256 | `TUI::out(u8"\n🤖 Telegram bot connected. Listening for messages...\n");` | Telegram 模式提示 |
| 261 | `TUI::out(u8"\nReady. Type your request (or '/help' '/h' for commands):\n");` | 就绪提示 |
| 263 | `TUI::out(u8"  💡 Shell commands are auto-detected and executed directly.\n");` | Shell 自动检测提示 |
| 274 | `TUI::out("\n");` | 循环内补换行 |

### 2. `src/agent.cpp` — Agent 核心
| 行号 | 代码 | 说明 |
|------|------|------|
| 543 | `TUI::out("\nSaving session to long-term memory...\n");` | `save_session()` 提示 |
| 800 | `TUI::out("%s", run_planned(user_input, resp, on_token).c_str());` | 任务规划流水线结果输出 |

### 3. `src/command_dispatcher.cpp` — 命令分发
| 行号 | 代码 | 说明 |
|------|------|------|
| 27 | `TUI::out("\n");` | 执行斜杠命令前补换行 |

### 4. `src/llm_client.cpp` — LLM 客户端
| 行号 | 代码 | 说明 |
|------|------|------|
| 539 | `TUI::out("\n");` | 流式输出结束后补换行，保证提示符在新行 |

### 5. `src/safety_guard.cpp` — 安全守卫
| 行号 | 代码 | 说明 |
|------|------|------|
| 57 | `TUI::out(u8"⚠  Dangerous operation: %s\n", operation.c_str());` | 危险操作警告 |
| 71 | `TUI::out("   %s\n", message.c_str());` | `ask_user_confirm()` 确认消息 |
| 72 | `TUI::out("   Type 'y' to confirm, anything else to cancel: ");` | 确认输入提示 |

### 6. `src/multi_agent.cpp` — 多 Agent 网络
| 行号 | 代码 | 说明 |
|------|------|------|
| 472 | `TUI::out(u8"\n⏸  [Remote Confirm] Client: %s\n", chat_id.c_str());` | 远程确认请求头 |
| 473 | `TUI::out("   %s\n", message.c_str());` | 确认消息内容 |
| 474 | `TUI::out("   Type 'y' to confirm, anything else to cancel: ");` | 确认输入提示 |

### 7. `src/reply_mode_command.cpp` — /reply-mode 命令
| 行号 | 代码 | 说明 |
|------|------|------|
| 19-28 | `TUI::out("\n--- User Reply Mode ---\n" ...);` | 显示当前模式与用法说明 |
| 36 | `TUI::out("\n  User reply mode is already: %s\n", ...);` | 模式未变化提示 |
| 41 | `TUI::out("\n  User reply mode changed to: %s\n", ...);` | 模式切换成功提示 |

---

## 三、TUI::err / TUI::cerr 使用点

| 位置 | 说明 |
|------|------|
| `src/tui.cpp:49` | `TUI::err(fmt, ...)` 定义（printf 风格 → `std::cerr`，自动 flush） |
| `src/tui.cpp:78` | `TUI::OStream TUI::cerr;` 静态成员定义 |
| `include/logger.h:87` | Warn/Error 级别日志走 `TUI::cerr` |
| `tools/file_tool.cpp:1051,1053,1082,1084` | `show_arguments` / `show_preview` 异常信息走 `TUI::cerr` |

> `TUI::err()`（函数）目前**没有任何调用方**，仅声明与定义存在。

---

## 四、使用约定总结

1. **流式拼接 / ANSI 控制序列** → 用 `TUI::cout <<`（如 key_watcher 的光标定位、tool.cpp 的 JSON 展示）。
2. **printf 格式化 / 需要广播到远程客户端** → 用 `TUI::out(fmt, ...)`（内部会 `agent::send_event("out", buf)`）。
3. **错误 / 警告** → `TUI::cerr <<` 或 `TUI::err()`。
4. **日志** → 统一走 `LOG_DEBUG/INFO/WARN/ERROR` 宏（`include/logger.h`），底层落到 `TUI::cout` / `TUI::cerr`。
5. 全局静默：`TUI::set_output_enabled(false)` 只影响 `out`/`err`/`printDim`，不影响 `TUI::cout` 流式输出。

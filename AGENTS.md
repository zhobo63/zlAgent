## Use zh-tw

## 如果遇到規格不清楚的時候 一定要再跟使用者確認 不要直接實作

## Cross-platform coding style

- Use #define stubs and empty functions to unify platform APIs at the top level, keeping business logic free of #ifdef.
- Example: define `getch`/`kbhit` as `_getch`/`_kbhit` on Windows, real impl on Linux; use empty `init_keyboard()`/`close_keyboard()` stubs where one platform doesn't need them.

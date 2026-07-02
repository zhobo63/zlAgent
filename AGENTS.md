## Use zh-tw

## Cross-platform coding style

- Use #define stubs and empty functions to unify platform APIs at the top level, keeping business logic free of #ifdef.
- Example: define `getch`/`kbhit` as `_getch`/`_kbhit` on Windows, real impl on Linux; use empty `init_keyboard()`/`close_keyboard()` stubs where one platform doesn't need them.

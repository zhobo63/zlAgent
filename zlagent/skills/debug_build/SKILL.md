# Debug Build Skill

## Description
Debug compilation errors by running the build, parsing error output, locating the problem, and suggesting fixes.

## When to Use
When the user asks you to fix build errors, debug compilation failures, or resolve linker issues.

## Instructions
1. Run the build command using `run_build` (or `execute_command` if no build system detected)
2. Parse the compiler output for error messages with file:line references
3. Read the source files at the reported locations using `read_file`
4. Use `grep_with_context` to find related code patterns around the errors
5. Suggest specific fixes based on the error type:
   - Missing includes → add #include directives
   - Type mismatches → correct types or add casts
   - Linker errors → check for missing object files or libraries
6. Apply fixes using `edit_file` if user confirms

## Tools Required
- run_build
- read_file
- grep_with_context

## Configuration
max_errors: 10
auto_fix: false

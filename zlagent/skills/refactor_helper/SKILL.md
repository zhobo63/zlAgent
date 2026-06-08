# Refactor Helper Skill

## Description
Refactor existing code to improve structure, readability, and maintainability without changing behavior.

## When to Use
When the user asks you to refactor, restructure, clean up, or optimize existing code while preserving its functionality.

## Instructions
1. Read the target file(s) using `read_file`
2. Identify refactoring opportunities:
   - Long functions that can be split into smaller ones
   - Duplicated code that can be extracted into shared utilities
   - Poor naming conventions that obscure intent
   - Deep nesting that can be flattened with early returns or guard clauses
   - Magic numbers/strings that should become named constants
3. Propose the refactoring plan to the user before making changes
4. Apply changes incrementally using `edit_file`, one logical unit at a time
5. After each batch of changes, verify the build compiles with `run_build`

## Tools Required
- read_file
- edit_file
- run_build

## Configuration
max_files: 3
auto_verify_build: true

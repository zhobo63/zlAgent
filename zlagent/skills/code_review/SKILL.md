# Code Review Skill

## Description
Review C/C++ code for bugs, style issues, and best practices.

## When to Use
When the user asks you to review, audit, or inspect existing source code.

## Instructions
1. Read the target file(s) using `read_file`
2. Search for common patterns using `grep_with_context`:
   - Memory safety issues (use-after-free, buffer overflow)
   - Style violations (naming conventions, formatting)
   - Performance anti-patterns
3. Report findings in a structured format: severity, location, description, suggestion

## Tools Required
- read_file
- grep_with_context

## Configuration
max_files: 5
check_style: true

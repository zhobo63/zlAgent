# Project Setup Skill

## Description
Initialize a new project with proper directory structure, build configuration, and git setup.

## When to Use
When the user asks you to create or initialize a new project from scratch.

## Instructions
1. Ask the user for project name and language (C++, JavaScript, Python, etc.) if not specified
2. Create the directory structure using `create_directory`:
   - src/ for source code
   - include/ for headers (if C/C++)
   - tests/ for test files
3. Write a basic build configuration file:
   - CMakeLists.txt for C++ projects
   - package.json for JavaScript/TypeScript projects
   - setup.py or pyproject.toml for Python projects
4. Create a .gitignore file appropriate for the project type
5. Initialize git with `execute_command` if not already initialized

## Tools Required
- create_directory
- write_file
- list_directory
- execute_command

## Configuration
default_language: cpp
include_git_init: true

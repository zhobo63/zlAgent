import os
import re

files = []
for root_dir in ["src", "include"]:
    if os.path.isdir(root_dir):
        for f in os.listdir(root_dir):
            if f.endswith(".cpp") or f.endswith(".h"):
                files.append(os.path.join(root_dir, f))

# Skip httplib.h - it's a third-party header
files = [f for f in files if "httplib" not in f]

for filepath in sorted(files):
    try:
        with open(filepath, "r", encoding="utf-8") as f:
            content = f.read()
            lines = content.split("\n")

            # Method 1: Check regular string literals on each line (skip comments)
            for i, line in enumerate(lines, 1):
                # Strip single-line comments first
                code_part = re.sub(r"//.*$", "", line)
                j = 0
                while j < len(code_part):
                    if code_part[j] == '"':
                        has_u8 = (j >= 2 and code_part[j - 2 : j] == "u8") or (
                            j >= 3 and code_part[j - 3 : j] == "u8 "
                        )
                        k = j + 1
                        while k < len(code_part) and code_part[k] != '"':
                            if code_part[k] == "\\" and k + 1 < len(code_part):
                                k += 2
                            else:
                                k += 1
                        if k < len(code_part):
                            s = code_part[j + 1 : k]
                            has_non_ascii = any(ord(c) > 127 for c in s)
                            if has_non_ascii and not has_u8:
                                print(f"{filepath}:{i}: {s[:100]}")
                        j = k + 1
                    else:
                        j += 1

            # Method 2: Check raw strings R"(...)" - these don't have u8 prefix typically
            raw_pattern = re.compile(r'R"\(([^)]*)\)"', re.DOTALL)
            for m in raw_pattern.finditer(content):
                s = m.group(1)
                has_non_ascii = any(ord(c) > 127 for c in s)
                if has_non_ascii:
                    start_pos = m.start()
                    line_num = content[:start_pos].count("\n") + 1
                    print(f"{filepath}:{line_num}: [raw string] {s[:100]}")

            # Method 3: Check for R"delim(...)" with custom delimiters
            raw_delim_pattern = re.compile(
                r'R"([a-zA-Z0-9_]{0,16})\((.*?)\)\1"', re.DOTALL
            )
            for m in raw_delim_pattern.finditer(content):
                s = m.group(2)
                has_non_ascii = any(ord(c) > 127 for c in s)
                if has_non_ascii:
                    start_pos = m.start()
                    line_num = content[:start_pos].count("\n") + 1
                    print(f"{filepath}:{line_num}: [raw string] {s[:100]}")

    except Exception as e:
        pass

print("Done.")

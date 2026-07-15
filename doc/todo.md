# ZL Agent 優化清單


## [ ] llm供應介面

doc/llm_provider.md

## GenerateFileOutline

```
# File outline for tools/file_tool.cpp (1812)
  14 namespace agent
  17   class ReadFileTool
 111 class ReadFilesTool
 293   struct FileTask
 332     namespace fs     <-- 無意義
 367 class WriteFileTool
 447   namespace fs       <-- 無意義
 478 namespace fs         <-- 無意義
 480 to_time_t()
 481   namespace std      <-- 無意義
 492 class DeleteFilesTool
 521   namespace fs       <-- 無意義
 584     namespace fs     <-- 無意義
 618       namespace fs   <-- 無意義
 639       namespace fs   <-- 無意義
 694 class EditFileTool
 862 class EditFilesTool
 957   [&]()  <-- 無意義
1042 class ReplaceTextMode
1044 is_json_array()
1126 [&]()    <-- 無意義
1135 [&]()    <-- 無意義
1143 [&]()    <-- 無意義
1151 [&]()    <-- 無意義
1159 [&]()    <-- 無意義
1238 class ListDirectoryTool
```

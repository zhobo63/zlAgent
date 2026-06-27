# line_range_edit

## Description
用行號範圍精確修改檔案內容，無需尋找舊文字

## When to Use
當你知道要修改的程式碼在哪幾行時，直接指定行號範圍進行替換

## Instructions
## 工作流程

1. **讀取目標行數**：使用 `read_file_lines` 讀取 start_line 到 end_line 的內容
2. **確認內容**：向使用者顯示要修改的區域，確保正確性
3. **執行替換**：使用 `edit_file` 將該範圍的文字替換為新內容

## 注意事項

- 如果檔案中有多處相同的文字，行號定位比文字匹配更可靠
- 替換後建議讀取檔案確認結果
- 對於大區塊修改（超過 20 行），考慮使用 `write_file` 覆蓋整個檔案

## 範例

使用者說：「把第 5 到 10 行的內容改成 XXX」
→ read_file_lines(path, start_line=5, end_line=10)
→ edit_file(old_text=讀取到的文字, new_text="XXX")

## Tools Required
- read_file_lines
- edit_file


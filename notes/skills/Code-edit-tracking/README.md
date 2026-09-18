# C 代码修改追踪 Skill

## 功能
在 AI 辅助编写或修改 C 语言代码时，强制要求 AI：
1. 询问修改者名称。
2. 在改动处添加规范的块注释（包含修改类型、修改者、时间、范围）。

## 使用方法
- **如果您使用 Claude Projects / ChatGPT Custom Instructions**：
  将 `system_prompt.md` 的内容复制到系统指令框中，将 `format_template.c` 上传至知识库作为参考文件。
- **如果您使用 API 开发 Agent**：
  读取 `system_prompt.md` 的内容作为 System Message，读取 `format_template.c` 作为上下文或 Few-shot 示例注入到 Prompt 中。
- **如果您使用 Cursor / Windsurf 等 AI IDE**：
  在项目根目录创建 `.cursorrules` 或相应的规则文件，将 `system_prompt.md` 和 `format_template.c` 的内容合并写入其中。
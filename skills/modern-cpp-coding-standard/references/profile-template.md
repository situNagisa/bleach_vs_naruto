# 项目 profile 模板

项目 profile 在核心默认值上增加限制，并记录会影响实现选择的项目决策。保持简短。

```yaml
language:
  standard: "c++latest"       # 或 c++23、c++20 等更低上限
  compiler: "..."
format:
  indentation: tabs
  line_length: 120              # 可选的更严格限制
namespaces:
  global_qualification: required
  adl: explicit-only
files:
  header_extension: .h
  forward_declarations: forbidden
errors:
  runtime: exceptions
  contracts: language-contracts-or-assert
  unrecoverable: terminate
documentation:
  public_api: doxygen
architecture:
  # ABI、模块、框架、并发和所有权决策
```

使用项目实际采用的术语和工具。Profile 可以增加必需的命名空间前缀、框架组件约定、ABI 导出宏或特定并发图等限制。没有项目依据时，不要把其他项目的架构复制进来。

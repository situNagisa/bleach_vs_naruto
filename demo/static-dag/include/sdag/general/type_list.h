#pragma once

/// 类型列表：一组类型怎么查、去重、拼接、映射。依赖：无。
///
///   type_list<Ts...>, at_t<I, List>     type_list/type_list.h
///   index_of_v<T, List>                 type_list/index_of.h   T 第一次出现的位置，不存在时等于 List::size
///   concat_t<Lists...>                  type_list/concat.h
///   unique_t<List>                      type_list/unique.h     保留首次出现的顺序去重
///   transform_t<Fn, List>               type_list/transform.h  每个元素套一层单参数模板
///   rebind_t<To, List>                  type_list/rebind.h     元素喂给另一个模板
///   general::refl::*                    type_list/reflection.h 反射实现共用的基础（arguments、list、plain……）
///
/// 除 type_list.h 外，每个文件都有反射与模板两种实现，用 SDAG_REFLECTION 隔开，对外名字相同。

#include "type_list/type_list.h"
#include "type_list/reflection.h"
#include "type_list/index_of.h"
#include "type_list/concat.h"
#include "type_list/unique.h"
#include "type_list/transform.h"
#include "type_list/rebind.h"

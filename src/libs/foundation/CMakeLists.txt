# ps_foundation：最底层的纯契约头（INTERFACE 库）
#
# 只放被所有层共享、但不属于任何机制的公共头（当前仅 Result.h）。
# 它是 header-only 的 INTERFACE 库：无 .cpp、无链接产物，仅提供 include 路径。
# 下沉目的：dataset / registry / intent / uischema 等机制库都需要 Result，
# 若让 Result 留在 ps_context 会形成 core ↔ 机制库 的 CMake 循环；
# 若随某个机制库走又会污染所有不碰该机制的消费者。故归位到最底层。
add_library(ps_foundation INTERFACE)
target_include_directories(ps_foundation INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
# Result.h 以 QString 承载 code/message，故最底层也须显式携带 Qt6::Core，
# 让任意消费者只链 ps_foundation 即可编译，无需自己知道要补 Qt。
target_link_libraries(ps_foundation INTERFACE Qt6::Core)

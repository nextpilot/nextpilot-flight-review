# ps_dataset：数据地基（Dataset 门面 + DataRepository 仓库）
#
# 只依赖 Rust kernel（ps_kernel）+ Qt Core + ps_foundation（Result.h），
# 不碰 core 任何业务类型（source / parser / intent / 飞行 / CAN）。
# 任何想用数据模型的消费者可只链本库，不必吞下整个 ps_context。
add_library(ps_dataset STATIC
    src/Dataset.cpp
    src/DatasetHub.cpp
    src/SourceHub.cpp
    include/Dataset.h
    include/DatasetHub.h
    include/Source.h
    include/DataSource.h
    include/SourceHub.h
    include/ChannelMime.h
)

target_include_directories(ps_dataset PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# ps_kernel: Rust ps-abi（Dataset 的 C ABI 门）；ps_foundation: Result.h（DataRepository 返回类型）。
# Qt6::Gui：ChannelMime.h 的 QMimeData 属 QtGui 值类型，不是 Widgets，headless 亦可链。
target_link_libraries(ps_dataset PUBLIC ps_kernel ps_registry ps_foundation Qt6::Core Qt6::Gui)
set_target_properties(ps_dataset PROPERTIES AUTOMOC ON CXX_STANDARD 17)

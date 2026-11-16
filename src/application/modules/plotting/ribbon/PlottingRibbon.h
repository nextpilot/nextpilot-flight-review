#pragma once
class QWidget;
namespace ps::plotting
{
void buildLayoutPicker(QWidget *panel);
void buildPagePanel(QWidget *panel);      // 页面：新建页 + 添加/删除/左右上下拆分，小按钮两行
void buildDisplayPanel(QWidget *panel);   // 显示：网格/边框/图例，两行
void buildPanelsToolbar(QWidget *panel);  // 画布操作：放大/缩小/移动/自适应，两行
void buildPlotGallery(QWidget *panel);
}

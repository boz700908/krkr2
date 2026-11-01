//---------------------------------------------------------------------------
// NarrationBridge.h —— environ 侧总入口：把钩子接到规则核心上
//
//   NarrationSink.h       钩子（visual 用，标准库）
//   NarrationPlatform.h   发声 / 音效 / 振动 / 剪贴板 / 点击注入（分平台）
//   NarrationCore.h       规则（平台无关，只有 environ 编译）
//   NarrationBridge.h     本文件：安装钩子
//   NarrationUi.h         引擎 UI 表单控件快照
//---------------------------------------------------------------------------
#pragma once

#include <string>

#include "NarrationCore.h"
#include "NarrationSink.h"

namespace TVPNarration {
namespace detail {

inline void LayerTextFromEngine(long layer, int x, int y, int w, int h,
                                const std::string &utf8) {
    TVPNarration::NotifyLayerText(layer, x, y, w, h, utf8);
}

} // namespace detail

// environ 启动时调用一次（TVPWindow.cpp 的静态初始化里）
inline void InstallLayerTextSink() { SetLayerTextSink(&detail::LayerTextFromEngine); }

} // namespace TVPNarration

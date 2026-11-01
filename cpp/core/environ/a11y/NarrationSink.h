//---------------------------------------------------------------------------
// NarrationSink.h —— 唯一的「visual → 朗读」通道（只依赖标准库）
//
// core_visual 不能依赖 cocos2d / JNI / environ 的窗口符号，所以这里只放一个
// 函数指针钩子：visual 层调用 NotifyLayerText()，environ 启动时把
// NarrationBridge.h 里的真正实现装进来。inline 函数的函数内静态变量由链接器
// 合并为同一实体，跨静态库也成立。
//---------------------------------------------------------------------------
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace TVPNarration {

typedef void (*LayerTextHook)(long layer, int x, int y, int w, int h, const std::string &utf8);

inline LayerTextHook &LayerTextSink() {
    static LayerTextHook hook = nullptr;
    return hook;
}

inline void SetLayerTextSink(LayerTextHook hook) { LayerTextSink() = hook; }

inline void DispatchLayerText(long layer, int x, int y, int w, int h, const std::string &utf8) {
    LayerTextHook hook = LayerTextSink();
    if(hook)
        hook(layer, x, y, w, h, utf8);
}

// 引擎宽字符（char16_t / 2 字节 wchar_t / 4 字节 wchar_t）→ UTF-8
template <typename CharT>
inline std::string ToUtf8(const CharT *text, size_t length) {
    std::string out;
    if(!text || length == 0)
        return out;
    out.reserve(length * 3);

    for(size_t i = 0; i < length; ++i) {
        uint32_t cp = static_cast<uint32_t>(text[i]);
        if(sizeof(CharT) == 2) {
            if(cp >= 0xD800 && cp <= 0xDBFF && i + 1 < length) {
                const uint32_t low = static_cast<uint32_t>(text[i + 1]);
                if(low >= 0xDC00 && low <= 0xDFFF) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    ++i;
                }
            } else if(cp >= 0xD800 && cp <= 0xDFFF) {
                cp = 0xFFFD;
            }
        }
        if(cp == 0)
            break;

        if(cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if(cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if(cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

// visual 层的便利入口：直接把引擎宽文本交给钩子
template <typename CharT>
inline void NotifyLayerTextWide(long layer, int x, int y, int w, int h, const CharT *text,
                                size_t length) {
    if(w <= 0 || h <= 0 || !text)
        return;
    if(length == 0) {  // 允许只给以 0 结尾的文本
        while(text[length])
            ++length;
    }
    if(length == 0)
        return;
    DispatchLayerText(layer, x, y, w, h, ToUtf8(text, length));
}

// ---- 图像坐标光标移动 ----
// 无障碍层移动焦点时，把游戏自己的鼠标也移过去：hover 效果、悬停音效、
// 按钮高亮等游戏自有表现因此不会被无障碍层「跳过」。
typedef void (*MouseMoveFn)(int imageX, int imageY);

inline MouseMoveFn &MouseMoveHandler() {
    static MouseMoveFn fn = nullptr;
    return fn;
}

inline void SetMouseMoveHandler(MouseMoveFn fn) { MouseMoveHandler() = fn; }

inline void NotifyMouseMove(int imageX, int imageY) {
    if(MouseMoveHandler())
        MouseMoveHandler()(imageX, imageY);
}

// 屏幕像素坐标的鼠标移动：界面控件的坐标是像素，引擎内部要的是图像坐标，
// 换算交给引擎侧（它知道当前图层的缩放比）。
typedef void (*HoverPixelsFn)(int px, int py);

inline HoverPixelsFn &HoverPixelsHandler() {
    static HoverPixelsFn fn = nullptr;
    return fn;
}

inline void SetHoverPixelsHandler(HoverPixelsFn fn) { HoverPixelsHandler() = fn; }

inline void NotifyHoverPixels(int px, int py) {
    if(HoverPixelsHandler())
        HoverPixelsHandler()(px, py);
}

// ---- 图像坐标右键 ----
// 长按手势 = 游戏自己的「右键」：KAG 用它打开右键菜单 / 隐藏对话框，
// 这是游戏自带的处理，绝不能由无障碍层代替。
typedef void (*RightClickFn)(int imageX, int imageY);

inline RightClickFn &RightClickHandler() {
    static RightClickFn fn = nullptr;
    return fn;
}

inline void SetRightClickHandler(RightClickFn fn) { RightClickHandler() = fn; }

// x/y 传负数表示「用游戏当前光标位置」
inline void NotifyRightClick(int imageX, int imageY) {
    if(RightClickHandler())
        RightClickHandler()(imageX, imageY);
}

//---------------------------------------------------------------------------
// 脚本图层里的「图片按钮」：KAG 的按钮就是带鼠标事件的图层，名字和矩形都在引擎里，
// 不需要 OCR。label 一律用图层内部名（Layer.name），拿不到名字的图层不收。
// x/y 是图层矩形中心，位于窗口图像坐标系（与引擎鼠标事件同一空间）。
//---------------------------------------------------------------------------
struct LayerButton {
    std::string label;
    int x;
    int y;
};

typedef void (*CollectLayerButtonsFn)(std::vector<LayerButton> &out);

inline CollectLayerButtonsFn &LayerButtonsSink() {
    static CollectLayerButtonsFn hook = nullptr;
    return hook;
}

inline void SetCollectLayerButtonsHandler(CollectLayerButtonsFn fn) { LayerButtonsSink() = fn; }

inline void CollectLayerButtons(std::vector<LayerButton> &out) {
    if(LayerButtonsSink())
        LayerButtonsSink()(out);
}

//---------------------------------------------------------------------------
// 在窗口图像坐标处注入一次左键按下 + 抬起（图片按钮激活用，全平台可用）
//---------------------------------------------------------------------------
typedef void (*TapImageFn)(int imageX, int imageY);

inline TapImageFn &TapImageHandler() {
    static TapImageFn hook = nullptr;
    return hook;
}

inline void SetTapImageHandler(TapImageFn fn) { TapImageHandler() = fn; }

inline void NotifyTapImage(int imageX, int imageY) {
    if(TapImageHandler())
        TapImageHandler()(imageX, imageY);
}

} // namespace TVPNarration

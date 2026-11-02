//---------------------------------------------------------------------------
// NarrationUi.h —— 引擎原生表单 → 朗读页面模型（environ 内部使用，全平台）
//
// 对应 CPyMO 里「每个页面自己报自己的焦点项」的做法：这里把 cocos2d 控件树
// 翻译成与 NarrationCore 无关的 Page（标题 + 一串 PageItem），交给
// NarrationCore 决定说什么；焦点索引保存在 Core 里，本文件负责
//   ① 把焦点项「激活」= 在该控件中心注入一次真实触摸
//      （GLView::handleTouchesBegin/End，与 Java 真实点击同一条路径）；
//   ② 表单栈变化时通知 Core 进页/出页。
//
// 页面模型来源：
//   * 滚动容器（ListView/ScrollView/PageView/TableView）的每个直接子节点
//     视为「一行」，行内第一个文本为标题，第二文本为值，复选框/滑条/输入框
//     决定行类型 —— 这样设置页读作「名字 值」一整句，和 CPyMO 一致；
//   * 其余容器递归，按钮/复选框/文本按视觉顺序各自成项；
//   * 名为 "title" 的节点（引擎导航栏标题）作为页面标题，不重复成项。
//---------------------------------------------------------------------------
#pragma once

#include <cocos2d.h>
#include <cocos-ext.h>
#include <cocos/ui/CocosGUI.h>

#include <string>
#include <vector>

#include "NarrationCore.h"

namespace TVPNarration {

class Node;

// 表单栈/表单内容变化时重建页面模型（定义在本文件末尾，外层命名空间）
inline void NotifyUiChanged(cocos2d::Node *container);

namespace detail_ui {

// 真实点击注入：与 Java nativeTouchesBegin/End 走同一条 GLView 通路
inline void InjectTapPixels(float px, float py) {
    cocos2d::GLView *glview = cocos2d::Director::getInstance()->getOpenGLView();
    if(!glview)
        return;
    intptr_t ids[1] = { 0x7A11 };
    float xs[1] = { px };
    float ys[1] = { py };
    glview->handleTouchesBegin(1, ids, xs, ys);
    glview->handleTouchesEnd(1, ids, xs, ys);
}

// 真实拖拽注入：按下 → 移动 → 抬起（滑条按拖动改值）
inline void InjectDragPixels(float fromX, float fromY, float toX, float toY) {
    cocos2d::GLView *glview = cocos2d::Director::getInstance()->getOpenGLView();
    if(!glview)
        return;
    intptr_t ids[1] = { 0x7A12 };
    float xs[1] = { fromX };
    float ys[1] = { fromY };
    glview->handleTouchesBegin(1, ids, xs, ys);
    xs[0] = toX;
    ys[0] = toY;
    glview->handleTouchesMove(1, ids, xs, ys);
    glview->handleTouchesEnd(1, ids, xs, ys);
}

// cocos 设计坐标（左下原点）→ 屏幕像素（左上原点），与 MotionEvent 输入互逆
inline bool WorldToPixels(const cocos2d::Vec2 &world, float &px, float &py) {
    cocos2d::GLView *glview = cocos2d::Director::getInstance()->getOpenGLView();
    if(!glview)
        return false;
    const cocos2d::Size frame = glview->getFrameSize();
    const cocos2d::Rect vp = glview->getViewPortRect();
    const float sx = glview->getScaleX();
    const float sy = glview->getScaleY();
    if(sx == 0.0f || sy == 0.0f)
        return false;
    px = world.x * sx + vp.origin.x;
    py = frame.height - (world.y * sy + vp.origin.y);
    return true;
}

inline bool NodeCenterPixels(cocos2d::Node *node, float &px, float &py) {
    if(!node)
        return false;
    const cocos2d::Size size = node->getContentSize();
    const cocos2d::Vec2 org = node->convertToWorldSpace(cocos2d::Vec2::ZERO);
    return WorldToPixels(
        cocos2d::Vec2(org.x + size.width / 2.0f, org.y + size.height / 2.0f), px, py);
}

inline bool IsVisibleChain(cocos2d::Node *node) {
    for(cocos2d::Node *cur = node; cur; cur = cur->getParent())
        if(!cur->isVisible())
            return false;
    return true;
}

inline std::string Clean(const std::string &s);

// 控件没有文字时的内部名字：Csd／代码里给的节点名，或者皮肤图/精灵的文件名。
// 绝不编造名字 —— 都拿不到就返回空，调用方据此跳过这一项。
inline std::string InternalNameOf(cocos2d::Node *node, int depth = 0) {
    if(!node || depth > 3)
        return std::string();
    const std::string name = Clean(node->getName());
    if(!name.empty())
        return name;
    // 图片按钮：cocos 3.17 的 SpriteFrame 没有保存纹理文件名，纹理路径也没有可移植接口，
    // 所以这里不猜名字，落到子节点递归；都拿不到就返回空（项仍在，只是不朗读）。
    const cocos2d::Vector<cocos2d::Node *> &children = node->getChildren();
    for(size_t i = 0; i < children.size(); ++i) {
        const std::string sub = InternalNameOf(children.at(i), depth + 1);
        if(!sub.empty())
            return sub;
    }
    return std::string();
}

inline std::string Clean(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    bool space = false;
    for(size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if(c == '\r')
            continue;
        if(c == '\n' || c == '\t' || c == ' ') {
            if(!space && !out.empty()) {
                out.push_back(' ');
                space = true;
            }
            continue;
        }
        out.push_back(c);
        space = false;
    }
    while(!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

inline void PushText(std::vector<std::string> &texts, const std::string &s) {
    const std::string c = Clean(s);
    if(c.empty())
        return;
    for(size_t i = 0; i < texts.size(); ++i)
        if(texts[i] == c)
            return;
    texts.push_back(c);
}

// 一行/一个控件的事实采集
struct Facts {
    std::vector<std::string> texts;
    bool hasCheck = false;
    bool checkState = false;
    bool hasSlider = false;
    int sliderPercent = 0;
    bool hasField = false;
    std::string fieldText;
    bool disabled = false;
};

inline void Gather(cocos2d::Node *node, Facts &f, int depth) {
    if(!node || depth > 6 || !node->isVisible())
        return;

    if(cocos2d::ui::CheckBox *cb = dynamic_cast<cocos2d::ui::CheckBox *>(node)) {
        f.hasCheck = true;
        f.checkState = cb->isSelected();
        if(!cb->isEnabled())
            f.disabled = true;
        return;
    }
    if(cocos2d::ui::Slider *sl = dynamic_cast<cocos2d::ui::Slider *>(node)) {
        f.hasSlider = true;
        f.sliderPercent = sl->getPercent();
        if(!sl->isEnabled())
            f.disabled = true;
        return;
    }
    if(cocos2d::ui::TextField *tf = dynamic_cast<cocos2d::ui::TextField *>(node)) {
        f.hasField = true;
        f.fieldText = Clean(tf->getString());
        if(!tf->isEnabled())
            f.disabled = true;
        return;
    }
    if(cocos2d::ui::Text *t = dynamic_cast<cocos2d::ui::Text *>(node)) {
        PushText(f.texts, t->getString());
        return;
    }
    if(cocos2d::Label *l = dynamic_cast<cocos2d::Label *>(node)) {
        PushText(f.texts, l->getString());
        return;
    }
    if(cocos2d::ui::Button *b = dynamic_cast<cocos2d::ui::Button *>(node)) {
        PushText(f.texts, b->getTitleText());
        if(!b->isEnabled())
            f.disabled = true;
    }
    if(cocos2d::ui::Widget *w = dynamic_cast<cocos2d::ui::Widget *>(node))
        if(!w->isEnabled())
            f.disabled = true;

    const cocos2d::Vector<cocos2d::Node *> &children = node->getChildren();
    for(size_t i = 0; i < children.size(); ++i)
        Gather(children.at(i), f, depth + 1);
}

inline bool IsScrollContainer(cocos2d::Node *node) {
    return dynamic_cast<cocos2d::ui::ListView *>(node) ||
           dynamic_cast<cocos2d::ui::ScrollView *>(node) ||
           dynamic_cast<cocos2d::ui::PageView *>(node) ||
           dynamic_cast<cocos2d::extension::TableView *>(node);
}

inline bool LooksLikeRow(cocos2d::Node *node) {
    // 滚动容器的直接子节点几乎都是「一行」：整行可点，内部有标题文本或控件
    if(!dynamic_cast<cocos2d::ui::Widget *>(node))
        return false;
    Facts f;
    Gather(node, f, 0);
    return !f.texts.empty() || f.hasCheck || f.hasSlider || f.hasField;
}

struct Model {
    Page page;
    std::vector<cocos2d::Node *> nodes;  // 与 page.items 一一对应
};

inline void AddItem(Model &m, const PageItem &item, cocos2d::Node *node) {
    m.page.items.push_back(item);
    m.nodes.push_back(node);
}

inline void AddRow(Model &m, cocos2d::Node *row) {
    Facts f;
    Gather(row, f, 0);

    PageItem item;
    item.caption = f.texts.empty() ? InternalNameOf(row) : f.texts[0];
    item.disabled = f.disabled;

    if(f.hasCheck) {
        item.kind = ITEM_CHECK;
        item.checked = f.checkState;
    } else if(f.hasSlider) {
        item.kind = ITEM_SLIDER;
        item.value = std::to_string(f.sliderPercent) + "%";
    } else if(f.hasField) {
        item.kind = ITEM_INPUT;
        item.value = f.fieldText;
    } else if(f.texts.size() > 1) {
        item.kind = ITEM_SELECT;
        item.value = f.texts[1];
    } else {
        item.kind = ITEM_ACTION;
    }
    AddItem(m, item, row);  // 没名字也保留：能聚焦、能激活，只是不读
}

inline void Collect(Model &m, cocos2d::Node *node, int depth) {
    if(!node || depth > 12 || !node->isVisible())
        return;

    if(dynamic_cast<cocos2d::ui::CheckBox *>(node) ||
       dynamic_cast<cocos2d::ui::Slider *>(node) ||
       dynamic_cast<cocos2d::ui::TextField *>(node)) {
        AddRow(m, node);
        return;
    }
    if(cocos2d::ui::Button *btn = dynamic_cast<cocos2d::ui::Button *>(node)) {
        std::string title = Clean(btn->getTitleText());
        if(title.empty())
            title = InternalNameOf(btn);  // 纯图片按钮：读它自己的内部名字
        {
            PageItem item;
            item.caption = title;  // 名字都拿不到就空着：项还在，只是不读
            item.kind = ITEM_ACTION;
            item.disabled = !btn->isEnabled();
            AddItem(m, item, btn);
            return;
        }
    }
    if(cocos2d::ui::Text *t = dynamic_cast<cocos2d::ui::Text *>(node)) {
        const std::string s = Clean(t->getString());
        if(!s.empty()) {
            PageItem item;
            item.caption = s;
            item.kind = ITEM_TEXT;
            AddItem(m, item, nullptr);
        }
        return;
    }

    if(cocos2d::Label *label = dynamic_cast<cocos2d::Label *>(node)) {
        const std::string s2 = Clean(label->getString());
        if(!s2.empty()) {
            PageItem item;
            item.caption = s2;
            item.kind = ITEM_TEXT;
            AddItem(m, item, nullptr);
        }
        return;
    }

    const cocos2d::Vector<cocos2d::Node *> &children = node->getChildren();
    if(IsScrollContainer(node)) {
        for(size_t i = 0; i < children.size(); ++i) {
            cocos2d::Node *child = children.at(i);
            if(!child->isVisible())
                continue;
            if(LooksLikeRow(child))
                AddRow(m, child);
            else
                Collect(m, child, depth + 1);
        }
        return;
    }
    for(size_t i = 0; i < children.size(); ++i)
        Collect(m, children.at(i), depth + 1);
}

inline cocos2d::Node *FindNamed(cocos2d::Node *root, const char *name, int depth = 0) {
    if(!root || depth > 12)
        return nullptr;
    if(root->getName() == name)
        return root;
    if(cocos2d::Node *direct = root->getChildByName(name))
        return direct;
    const cocos2d::Vector<cocos2d::Node *> &children = root->getChildren();
    for(size_t i = 0; i < children.size(); ++i)
        if(cocos2d::Node *found = FindNamed(children.at(i), name, depth + 1))
            return found;
    return nullptr;
}

// 导航栏标题（Csd 里固定叫 "title"，可能是 Button 也可能是 Text）
inline std::string FormTitle(cocos2d::Node *form, std::vector<cocos2d::Node *> &exclude) {
    cocos2d::Node *title = FindNamed(form, "title");
    if(!title)
        return std::string();
    exclude.push_back(title);
    if(cocos2d::ui::Button *b = dynamic_cast<cocos2d::ui::Button *>(title))
        return Clean(b->getTitleText());
    if(cocos2d::ui::Text *t = dynamic_cast<cocos2d::ui::Text *>(title))
        return Clean(t->getString());
    Facts f;
    Gather(title, f, 0);
    return f.texts.empty() ? std::string() : f.texts[0];
}

inline bool IsExcluded(const std::vector<cocos2d::Node *> &exclude, cocos2d::Node *node) {
    for(size_t i = 0; i < exclude.size(); ++i) {
        cocos2d::Node *e = exclude[i];
        for(cocos2d::Node *cur = node; cur; cur = cur->getParent())
            if(cur == e)
                return true;
    }
    return false;
}

inline Model BuildModel(cocos2d::Node *form, const std::vector<cocos2d::Node *> &exclude) {
    Model m;
    const cocos2d::Vector<cocos2d::Node *> &children = form->getChildren();
    for(size_t i = 0; i < children.size(); ++i) {
        cocos2d::Node *child = children.at(i);
        if(!child->isVisible() || IsExcluded(exclude, child))
            continue;
        Collect(m, child, 0);
    }
    return m;
}

// 当前快照（激活/焦点回调按索引取节点）
inline std::vector<cocos2d::Node *> &NodeRefs() {
    static std::vector<cocos2d::Node *> refs;
    return refs;
}

inline void OnActivate(int index) {
    std::vector<cocos2d::Node *> &refs = NodeRefs();
    if(index < 0 || index >= (int) refs.size())
        return;
    cocos2d::Node *node = refs[index];
    if(!node || !IsVisibleChain(node))
        return;
    float px = 0, py = 0;
    if(!NodeCenterPixels(node, px, py))
        return;
    InjectTapPixels(px, py);
}

// 焦点同步：把焦点行滚进可视区，并把游戏内鼠标一起移到该行
inline void OnFocus(int index) {
    std::vector<cocos2d::Node *> &refs = NodeRefs();
    if(index < 0 || index >= (int) refs.size())
        return;
    cocos2d::Node *focusNode = refs[index];
    if(focusNode && IsVisibleChain(focusNode)) {
        // 只移动、不点击：游戏自己的 hover 效果/悬停音效照旧发生
        float px = 0, py = 0;
        if(NodeCenterPixels(focusNode, px, py))
            NotifyHoverPixels((int) px, (int) py);
    }
    cocos2d::ui::Widget *widget = dynamic_cast<cocos2d::ui::Widget *>(refs[index]);
    if(!widget)
        return;
    for(cocos2d::Node *cur = widget->getParent(); cur; cur = cur->getParent()) {
        cocos2d::ui::ListView *list = dynamic_cast<cocos2d::ui::ListView *>(cur);
        if(!list)
            continue;
        const cocos2d::Vector<cocos2d::ui::Widget *> &items = list->getItems();
        for(ssize_t i = 0; i < (ssize_t) items.size(); ++i) {
            if(items.at((size_t) i) == widget) {
                list->scrollToItem((ssize_t) i, cocos2d::Vec2::ZERO,
                                   cocos2d::Vec2::ZERO, 0.2f);
                return;
            }
        }
        return;
    }
}

// 游戏内容根节点（剧情界面上扫按钮时要避开它）
inline cocos2d::Node *&GameRoot() {
    static cocos2d::Node *node = nullptr;
    return node;
}

// 当前表单栈节点（滑条调整后要据此重建页面模型）
inline cocos2d::Node *&CurrentContainer() {
    static cocos2d::Node *node = nullptr;
    return node;
}

// 焦点项的值为滑条时：按 ±5% 拖一档，然后立刻刷新页面模型（朗读新值）
inline void OnAdjust(int index, int delta) {
    std::vector<cocos2d::Node *> &refs = NodeRefs();
    if(index < 0 || index >= (int) refs.size())
        return;
    cocos2d::Node *node = refs[index];
    if(!node || !IsVisibleChain(node))
        return;
    cocos2d::ui::Slider *slider = dynamic_cast<cocos2d::ui::Slider *>(node);
    if(!slider)
        return;
    const cocos2d::Size size = node->getContentSize();
    const cocos2d::Vec2 org = node->convertToWorldSpace(cocos2d::Vec2::ZERO);
    float leftPx = 0, rightPx = 0, midPy = 0, ignored = 0;
    if(!WorldToPixels(cocos2d::Vec2(org.x, org.y + size.height / 2), leftPx, midPy))
        return;
    if(!WorldToPixels(cocos2d::Vec2(org.x + size.width, org.y + size.height / 2),
                      rightPx, ignored))
        return;
    const float widthPx = rightPx - leftPx;
    if(widthPx < 2.0f)
        return;
    const int percent = slider->getPercent();
    int target = percent + (delta >= 0 ? 5 : -5);
    if(target < 0) target = 0;
    if(target > 100) target = 100;
    if(target == percent)
        return;
    InjectDragPixels(leftPx + widthPx * (percent / 100.0f), midPy,
                    leftPx + widthPx * (target / 100.0f), midPy);
    NotifyUiChanged(CurrentContainer());
}

inline bool &Installed() {
    static bool installed = false;
    return installed;
}

// 当前展示的表单（用于判定「同一个页面内容变化」还是「换页」）
inline cocos2d::Node *&CurrentForm() {
    static cocos2d::Node *form = nullptr;
    return form;
}

// 同一张图上切出来的多项要能分辨：内部名字重复时按出现顺序补编号。
// 只在名字真正重复时才加，绝不拿编号替代名字。
inline void NumberDuplicateLabels(std::vector<StoryItem> &items) {
    std::vector<std::string> base(items.size());
    for(size_t i = 0; i < items.size(); ++i)
        base[i] = items[i].label;
    for(size_t i = 0; i < items.size(); ++i) {
        size_t same = 0, index = 0;
        if(base[i].empty())
            continue;  // 无名项不加编号：编号只能跟着真名字
        for(size_t j = 0; j < items.size(); ++j) {
            if(base[j] != base[i])
                continue;
            ++same;
            if(j <= i)
                ++index;
        }
        if(same > 1)
            items[i].label = base[i] + " " + std::to_string(index);
    }
}

// 剧情界面（没有表单）上的其他可点控件：台词由 Core 固定当第 0 项，
// 这里收它后面的按钮，并把 NodeRefs 对齐成 [nullptr, ...]，这样
// 「第 i 项被确认」= 该索引对应的节点（space==1 的图片按钮不用节点，走图像坐标点击）。
inline void RefreshStoryItems(cocos2d::Node *uiContainer) {
    std::vector<StoryItem> items;
    std::vector<cocos2d::Node *> refs;
    refs.push_back(nullptr);  // 第 0 项 = 台词，节点为空（由 Core 推进）

    // ① 脚本图层画的按钮：KAG 的图片按钮就是带事件的 Layer，
    //    名字和矩形来自引擎本身（图像坐标），不依赖 OCR，也不猜名字。
    {
        std::vector<LayerButton> layerButtons;
        CollectLayerButtons(layerButtons);
        for(size_t i = 0; i < layerButtons.size(); ++i) {
            const std::string label = Clean(layerButtons[i].label);  // 可能为空：项在，不读
            StoryItem si;
            si.label = label;
            si.px = layerButtons[i].x;
            si.py = layerButtons[i].y;
            si.space = 1;
            si.clickable = 1;
            items.push_back(si);
            refs.push_back(nullptr);
        }
    }

    // ② 引擎控件（Csd 表单）：像素坐标，激活时注入真实触摸
    if(uiContainer) {
        cocos2d::Node *root = uiContainer->getParent();
        cocos2d::Node *gameNode = GameRoot();
        if(root) {
            const cocos2d::Vector<cocos2d::Node *> &children = root->getChildren();
            for(size_t i = 0; i < children.size(); ++i) {
                cocos2d::Node *child = children.at(i);
                if(child == uiContainer || child == gameNode || !child->isVisible())
                    continue;
                Model m;
                Collect(m, child, 0);
                for(size_t k = 0; k < m.page.items.size(); ++k) {
                    const PageItem &item = m.page.items[k];
                    cocos2d::Node *node = m.nodes[k];
                    if(!node || item.disabled || item.kind == ITEM_TEXT)
                        continue;  // 只收能点的
                    std::string label = item.caption;
                    if(label.empty())
                        label = InternalNameOf(node);
                    if(!item.value.empty())
                        label += (label.empty() ? std::string() : std::string(" ")) + item.value;
                    // 名字拿不到也保留这一项：能聚焦、能把鼠标挪过去、能激活，只是不读
                    float px = 0, py = 0;
                    if(!NodeCenterPixels(node, px, py))
                        continue;
                    StoryItem si;
                    si.label = label;
                    si.px = (int) px;
                    si.py = (int) py;
                    si.space = 0;
                    si.clickable = 1;
                    items.push_back(si);
                    refs.push_back(node);
                }
            }
        }
    }

    // ③ 脚本层画出来的文字（标题界面的菜单名、存档列表、提示……）：
    //    引擎里有原文，直接当只读焦点项收进来，不需要 OCR。
    //    主台词那一层不重复收（它就是第 0 项），相同文字只留一条。
    {
        std::lock_guard<std::mutex> lock(detail::Mutex());
        const unsigned now = detail::NowMs();
        long mainLayer = -1;
        detail::LineState *mainLine = nullptr;
        detail::MainLineLocked(now, mainLayer, mainLine);
        std::vector<std::string> seen;
        for(std::map<long, detail::LineState>::iterator it = detail::Lines().begin();
            it != detail::Lines().end(); ++it) {
            if(it->first == mainLayer)
                continue;
            const detail::LineState &line = it->second;
            if(line.text.empty() || now - line.tick > detail::kStaleMs || line.w <= 0 ||
               line.h <= 0)
                continue;
            const std::string text = Clean(line.text);
            if(text.empty())
                continue;
            bool duplicate = false;
            for(size_t k = 0; k < seen.size(); ++k)
                if(seen[k] == text) {
                    duplicate = true;
                    break;
                }
            if(duplicate)
                continue;
            seen.push_back(text);
            StoryItem si;
            si.label = text;
            si.px = -1;
            si.py = -1;
            si.space = 0;
            si.clickable = 0;  // 只读：确认键只重读
            items.push_back(si);
            refs.push_back(nullptr);
            if(items.size() >= 60)
                break;
        }
    }

    NumberDuplicateLabels(items);
    NodeRefs() = refs;
    SetStoryItems(items);
}

} // namespace detail_ui

// 表单栈变化 / 表单内容变化时调用。container 为 UI 表单栈节点（空栈表示无表单）。
inline void NotifyUiChanged(cocos2d::Node *container) {
    using namespace detail_ui;

    if(!Installed()) {
        Installed() = true;
        SetPageHandlers(&OnActivate, &OnFocus, &OnAdjust);
    }
    if(!IsEnabled())
        return;

    CurrentContainer() = container;
    cocos2d::Node *top = nullptr;
    if(container && container->getChildrenCount() > 0)
        top = container->getChildren().back();

    if(!top) {
        if(CurrentForm()) {
            CurrentForm() = nullptr;
            NodeRefs().clear();
            PageClosed();
        }
        // 剧情界面：重新扫一遍画面上的按钮（游戏随时可能加/删按钮）
        RefreshStoryItems(container);
        return;
    }
    SetStoryItems(std::vector<StoryItem>());  // 进了表单，剧情焦点环下班

    const bool switched = (CurrentForm() != top);
    CurrentForm() = top;

    std::vector<cocos2d::Node *> exclude;
    const std::string title = FormTitle(top, exclude);   // 同时把标题节点加入 exclude
    Model model = BuildModel(top, exclude);
    model.page.title = title;
    NodeRefs() = model.nodes;

    if(switched)
        PageEntered(model.page);
    else
        PageUpdated(model.page);
}

inline void ResetUi();

// 每帧/定时轮询：表单栈换了就进页/出页，同页则刷新数值（Core 内部去重）
inline void PollUi(cocos2d::Node *container) {
    if(!IsEnabled()) {
        if(IsPageOpen())
            ResetUi();
        return;
    }
    NotifyUiChanged(container);
}

inline void PollUi(cocos2d::Node *container, cocos2d::Node *gameNode) {
    detail_ui::GameRoot() = gameNode;
    PollUi(container);
}

// 引擎强制关闭无障碍（例如游戏退出）
inline void ResetUi() {
    detail_ui::CurrentContainer() = nullptr;
    detail_ui::CurrentForm() = nullptr;
    detail_ui::NodeRefs().clear();
    PageClosed();
}

} // namespace TVPNarration

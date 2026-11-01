//---------------------------------------------------------------------------
// NarrationCore.h —— 无障碍朗读的「规则层」（平台无关）
//
// 结构完全照 CPyMO：
//   1. 手势只生产语义动作（Action），由本文件分发到「当前页面」；
//   2. 每个页面自己负责朗读自己的焦点项 —— 页面模型由 UI 层（NarrationUi.h）
//      在页面入栈/焦点变化时推上来，本文件只决定「说什么、什么音效、什么振动」；
//   3. 台词整句朗读：krkr2 的 DrawText 每帧传的是「已打到第 N 字」的前缀，
//      所以用「前缀增长=同一句、非前缀=新句」判定行身份，稳定后整句朗读，
//      新句出现时播 SOUND_ENTER（对应 CPyMO「新对话框=ENTER」）；
//   4. 音效三档、振动时长、复制语义、配置开关，全部与 CPyMO 一致；
//   5. 平台层（NarrationPlatform.h）只负责出声/振动/剪贴板/点击注入。
//---------------------------------------------------------------------------
#pragma once

#include <atomic>
#include "NarrationSink.h"
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "NarrationPlatform.h"

namespace TVPNarration {

// 设置面板 → 引擎的细粒度开关（Java 侧 A11yPrefs 映射成位）
enum ConfigFlag {
    FLAG_SPEAK_DIALOGUE = 1,     // 自动朗读新台词
    FLAG_SPEAK_CHOICES = 2,      // 朗读选项列表
    FLAG_SPEAK_SPEAKER = 4,      // 朗读说话人名
    FLAG_FEEDBACK_SOUND = 8,     // 操作音效
    FLAG_FEEDBACK_VIBRATE = 16,  // 振动反馈
    FLAG_ALL = 31
};

// 动作（0-11 与 CPyMO cpymo_accessibility_action 同值；REPEAT 为扩展）
enum Action {
    ACTION_NONE = 0,
    ACTION_UP = 1,
    ACTION_DOWN = 2,
    ACTION_LEFT = 3,
    ACTION_RIGHT = 4,
    ACTION_OK = 5,
    ACTION_CANCEL = 6,
    ACTION_SKIP = 7,
    ACTION_SKIP_HOLD_START = 8,
    ACTION_SKIP_HOLD_END = 9,
    ACTION_COPY = 10,
    ACTION_APPEND_COPY = 11,
    ACTION_REPEAT = 12,
    // 单击 = 只把游戏内的鼠标指针移到触点（CPyMO 的 onTap→onScan 语义），不点击
    ACTION_SCAN = 13,
    // 双指下滑 = 直接取消/返回（不看游戏有没有右键菜单）
    ACTION_BACK = 14
};

// 页面项类型：决定朗读措辞与激活方式
enum ItemKind {
    ITEM_ACTION = 0,  // 普通按钮/菜单项
    ITEM_SUBPAGE = 1, // 进入子页
    ITEM_CHECK = 2,   // 复选框（有开/关）
    ITEM_SELECT = 3,  // 下拉选择（有当前值）
    ITEM_SLIDER = 4,  // 滑条（有当前值）
    ITEM_INPUT = 5,   // 文本/按键输入（有当前值）
    ITEM_TEXT = 6     // 只读文本/分隔线（不进焦点序列）
};

struct PageItem {
    std::string caption;   // UTF-8
    std::string value;      // UTF-8，复选框为空（用 checked）
    int kind = ITEM_ACTION;
    bool checked = false;
    bool disabled = false;
};

struct Page {
    std::string title;          // UTF-8，可为空
    std::vector<PageItem> items;
};

namespace detail {

const unsigned kSettleMs = 260;      // 台词/选项稳定判定（CPyMO 直接整句，这里近似）
const unsigned kStaleMs = 15000;     // 图层文本过期
const unsigned kSkipIntervalMs = 150;
const size_t kMaxLineLen = 4000;

struct LineState {
    std::string text;   // 当前行的最新前缀
    unsigned tick = 0;  // 最后一次变化时间
    bool spoken = false;
    int x = 0, y = 0, w = 0, h = 0;
};

struct Choice {
    std::string text;
    int x = 0, y = 0, w = 0, h = 0;
};

// 页面激活/焦点回调：由 NarrationUi.h 安装（操作 cocos2d 控件树）
typedef void (*PageActivateFn)(int index);
typedef void (*PageFocusFn)(int index);
typedef void (*PageAdjustFn)(int index, int delta);  // 滑条等有值控件的 ±一档

inline std::mutex &Mutex() { static std::mutex m; return m; }
inline std::condition_variable &Cv() { static std::condition_variable c; return c; }
inline std::atomic<bool> &Enabled() { static std::atomic<bool> e{true}; return e; }
// 状态代数：页面开合、新台词、选项变化都会 +1。
// 长按右键后用它判断「游戏到底有没有响应右键」，没有响应才退回「返回」。
inline std::atomic<unsigned> &StateGen() { static std::atomic<unsigned> g{0}; return g; }
inline std::atomic<bool> &SkipRunning() { static std::atomic<bool> s{false}; return s; }
inline std::map<long, LineState> &Lines() { static std::map<long, LineState> l; return l; }
inline std::string &LastSpoken() { static std::string s; return s; }
inline bool &FeedbackFlag() { static bool g = false; return g; }

inline std::string &SpokenDialogue() { static std::string s; return s; }
inline std::vector<Choice> &Choices() { static std::vector<Choice> c; return c; }
inline std::string &SpokenChoicesKey() { static std::string k; return k; }
inline int &ChoiceFocus() { static int i = -1; return i; }
inline int &ScreenW() { static int w = 1280; return w; }
inline int &ScreenH() { static int h = 720; return h; }
inline bool &Started() { static bool s = false; return s; }
inline bool &SpeakerWithDialogue() { static bool b = true; return b; }
inline unsigned &ConfigFlags() { static unsigned f = FLAG_ALL; return f; }

// 当前页面
inline Page &CurrentPage() { static Page p; return p; }
inline int &PageFocusIndex() { static int i = -1; return i; }
inline bool &PageOpenFlag() { static bool o = false; return o; }
inline PageActivateFn &ActivateFn() { static PageActivateFn f = nullptr; return f; }
inline PageFocusFn &FocusFn() { static PageFocusFn f = nullptr; return f; }
inline PageAdjustFn &AdjustFn() { static PageAdjustFn f = nullptr; return f; }

inline unsigned NowMs() {
    using namespace std::chrono;
    static const steady_clock::time_point base = steady_clock::now();
    return static_cast<unsigned>(duration_cast<milliseconds>(steady_clock::now() - base).count());
}

inline bool IsSpaceByte(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

inline std::string Trim(const std::string &s) {
    size_t b = 0, e = s.size();
    while(b < e && IsSpaceByte(s[b])) ++b;
    while(e > b && IsSpaceByte(s[e - 1])) --e;
    return s.substr(b, e - b);
}

// 多行压成一段（朗读用）
inline std::string OneLine(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    bool lastSpace = false;
    for(size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if(c == '\r')
            continue;
        if(c == '\n' || c == '\t') {
            if(!lastSpace) { out.push_back(' '); lastSpace = true; }
            continue;
        }
        out.push_back(c);
        lastSpace = false;
    }
    return Trim(out);
}

inline size_t Utf8Length(const std::string &s) {
    size_t n = 0;
    for(size_t i = 0; i < s.size(); ++i)
        if((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80)
            ++n;
    return n;
}

inline bool HasSentenceEnd(const std::string &s) {
    static const char *marks[] = {"。", "！", "？", "…", "」", "!", "?", "."};
    for(size_t i = 0; i < sizeof(marks) / sizeof(marks[0]); ++i)
        if(s.find(marks[i]) != std::string::npos)
            return true;
    return false;
}

// ---- 朗读出口 ----
inline void Speak(const std::string &text, bool flush) {
    const std::string line = OneLine(text);
    if(line.empty())
        return;
    if(!FeedbackFlag())
        LastSpoken() = line;
    TVPNarrationPlatform::Speak(line, flush);
}

// 反馈语：说完不覆盖最后朗读的游戏文本（CPyMO copy_feedback_in_progress）
inline void SpeakFeedback(const std::string &msg) {
    if(msg.empty())
        return;
    const bool prev = FeedbackFlag();
    FeedbackFlag() = true;
    std::string saved = LastSpoken();
    TVPNarrationPlatform::Speak(msg, true);
    LastSpoken() = saved;
    FeedbackFlag() = prev;
}

inline void SoundAndVibrate(int sound, int vibrateMs) {
    const unsigned flags = ConfigFlags();
    if(flags & FLAG_FEEDBACK_SOUND)
        TVPNarrationPlatform::PlaySound(sound);
    if(vibrateMs > 0 && (flags & FLAG_FEEDBACK_VIBRATE))
        TVPNarrationPlatform::Vibrate(vibrateMs);
}

// ---------------- 页面朗读 ----------------

inline std::string PageItemSpeech(const PageItem &item) {
    // 名字拿不到的项目依然存在、依然可聚焦可激活，只是不读它：
    // 绝不编造「按钮 1」这类名字（编号只用于同名项区分，见 NarrationUi）。
    std::string state;
    switch(item.kind) {
        case ITEM_CHECK:
            state = item.checked ? "已选中" : "未选中";
            break;
        case ITEM_SELECT:
        case ITEM_SLIDER:
        case ITEM_INPUT:
            state = item.value;
            break;
        case ITEM_SUBPAGE:
            state = "子菜单";
            break;
        default:
            break;
    }
    if(item.disabled) {
        if(!state.empty())
            state += "，";
        state += "不可用";
    }
    if(item.caption.empty())
        return state;  // 没有名字：只剩状态，状态也没有就什么都不读
    if(state.empty())
        return item.caption;
    return item.caption + "，" + state;
}

// 焦点序列：跳过只读文本
inline int NextFocus(int from, int step) {
    Page &page = CurrentPage();
    const int n = (int) page.items.size();
    if(n <= 0)
        return -1;
    int idx = from;
    for(int guard = 0; guard < n; ++guard) {
        idx += step;
        if(idx < 0) idx = n - 1;
        if(idx >= n) idx = 0;
        return idx;
    }
    return from >= 0 && from < n ? from : -1;
}

inline int FirstFocus() {
    Page &page = CurrentPage();
    return page.items.empty() ? -1 : 0;
}

// 进入页面的朗读文本：「标题，第一项」合成一句（对应 CPyMO 进页报第一项）。
// 只在持锁时调用，取到字符串后由调用方在锁外发声。
inline std::string PageEntrySpeech() {
    if(!PageOpenFlag())
        return std::string();
    std::string msg;
    if(!CurrentPage().title.empty())
        msg = OneLine(CurrentPage().title);
    if(PageFocusIndex() >= 0 && PageFocusIndex() < (int) CurrentPage().items.size()) {
        const std::string item = PageItemSpeech(CurrentPage().items[PageFocusIndex()]);
        if(!msg.empty())
            msg += "，";
        msg += item;
    }
    return msg;
}

// 当前焦点项的朗读文本（持锁调用）
inline std::string PageFocusSpeech() {
    if(!PageOpenFlag())
        return std::string();
    if(PageFocusIndex() < 0 || PageFocusIndex() >= (int) CurrentPage().items.size())
        return std::string();
    return PageItemSpeech(CurrentPage().items[PageFocusIndex()]);
}

// ---------------- 图层台词 / 选项 ----------------

inline unsigned LineArea(const LineState &l) {
    if(l.w <= 0 || l.h <= 0)
        return 0;
    return (unsigned) l.w * (unsigned) l.h;
}

// 主对话行：面积最大且未过期的行
inline bool MainLineLocked(unsigned now, long &layer, LineState *&out) {
    out = nullptr;
    layer = -1;
    unsigned best = 0;
    for(std::map<long, LineState>::iterator it = Lines().begin(); it != Lines().end(); ++it) {
        LineState &l = it->second;
        if(l.text.empty() || now - l.tick > kStaleMs)
            continue;
        const unsigned area = LineArea(l);
        if(!out || area > best) {
            out = &l;
            layer = it->first;
            best = area;
        }
    }
    return out != nullptr;
}

// 选项：非主对话行、短文本、纵向排布、2~8 条（KAG 选项层没法拿焦点，只能靠几何）
inline void UpdateChoicesLocked(long mainLayer) {
    if(!(ConfigFlags() & FLAG_SPEAK_CHOICES)) {
        if(!Choices().empty()) {
            Choices().clear();
            ChoiceFocus() = -1;
            SpokenChoicesKey().clear();
        }
        return;
    }
    std::vector<Choice> found;
    for(std::map<long, LineState>::iterator it = Lines().begin(); it != Lines().end(); ++it) {
        if(it->first == mainLayer)
            continue;
        LineState &l = it->second;
        const std::string line = OneLine(l.text);
        if(line.empty())
            continue;
        const size_t len = Utf8Length(line);
        if(len == 0 || len > 40)
            continue;
        Choice c;
        c.text = line;
        c.x = l.x; c.y = l.y; c.w = l.w; c.h = l.h;
        found.push_back(c);
    }
    bool changed = (found.size() != Choices().size());
    if(!changed) {
        for(size_t i = 0; i < found.size(); ++i)
            if(found[i].text != Choices()[i].text) {
                changed = true;
                break;
            }
    }
    if(changed)
        StateGen().fetch_add(1, std::memory_order_relaxed);
    if(found.size() >= 2 && found.size() <= 8) {
        Choices() = found;
        if(ChoiceFocus() >= (int) Choices().size())
            ChoiceFocus() = -1;
    } else {
        Choices().clear();
        ChoiceFocus() = -1;
        SpokenChoicesKey().clear();
    }
}

inline std::string JoinChoices() {
    std::string key;
    for(size_t i = 0; i < Choices().size(); ++i) {
        key += '\x1f';
        key += Choices()[i].text;
    }
    return key;
}

// 说话人：主对话行上方的短文本（CPyMO：名字 + 换行 + 台词，一次说完）
inline std::string SpeakerPrefixLocked(const LineState *dialogue) {
    if(!(ConfigFlags() & FLAG_SPEAK_SPEAKER))
        return std::string();
    if(!SpeakerWithDialogue() || !dialogue)
        return std::string();
    const std::string dialogueText = OneLine(dialogue->text);
    std::string best;
    int bestY = -0x7fffffff;
    for(std::map<long, LineState>::iterator it = Lines().begin(); it != Lines().end(); ++it) {
        const LineState &l = it->second;
        if(&l == dialogue)
            continue;
        const std::string line = OneLine(l.text);
        if(line.empty())
            continue;
        const size_t len = Utf8Length(line);
        if(len == 0 || len > 16)
            continue;
        if(HasSentenceEnd(line))
            continue;
        if(l.y < dialogue->y)  // 图像坐标 y 向上：说话人在对话上方
            continue;
        if(dialogueText.find(line) == 0)  // 台词里已含名字
            continue;
        if(l.y >= bestY) {  // 取最靠近对话的那一行
            bestY = l.y;
            best = line;
        }
    }
    return best;
}


// 每 50ms 推进一次的规则（只有「稳定判定」需要它；其余都是事件驱动）
inline void Tick() {
    if(!Enabled().load(std::memory_order_relaxed))
        return;

    const unsigned now = NowMs();

    if(SkipRunning().load(std::memory_order_relaxed)) {
        static unsigned lastAdvance = 0;
        if(now - lastAdvance >= kSkipIntervalMs) {
            lastAdvance = now;
            TVPNarrationPlatform::TapScreen(ScreenW() / 2, static_cast<int>(ScreenH() * 0.85f));
        }
    }

    std::string toSpeak;
    std::string choicesSpeech;

    {
        std::lock_guard<std::mutex> lock(Mutex());

        // 过期清理
        for(std::map<long, LineState>::iterator it = Lines().begin(); it != Lines().end();) {
            if(now - it->second.tick > kStaleMs)
                it = Lines().erase(it);
            else
                ++it;
        }

        // 页面打开时不朗读图层文本（CPyMO 同样：菜单与对白互斥）
        if(PageOpenFlag())
            return;

        long mainLayer = -1;
        LineState *dialogue = nullptr;
        if(!MainLineLocked(now, mainLayer, dialogue))
            return;

        UpdateChoicesLocked(mainLayer);

        const std::string current = OneLine(dialogue->text);
        if(current.empty())
            return;
        if(now - dialogue->tick < kSettleMs)
            return;  // 还在打字

        // 选项列表：一组新选项出现时报一次
        if(Choices().size() >= 2) {
            const std::string key = JoinChoices();
            if(key != SpokenChoicesKey()) {
                SpokenChoicesKey() = key;
                for(size_t i = 0; i < Choices().size(); ++i) {
                    if(i) choicesSpeech += "。";
                    choicesSpeech += "选项 " + std::to_string(i + 1) + "，";
                    choicesSpeech += Choices()[i].text;
                }
            }
        }

        if(!dialogue->spoken && current != SpokenDialogue()) {
            dialogue->spoken = true;
            SpokenDialogue() = current;
            if(ConfigFlags() & FLAG_SPEAK_DIALOGUE) {
                const std::string speaker = SpeakerPrefixLocked(dialogue);
                toSpeak = speaker.empty() ? current : (speaker + "：" + current);
            }
        }
    }

    if(!toSpeak.empty())
        Speak(toSpeak, true);
    if(!choicesSpeech.empty())
        TVPNarrationPlatform::Speak(choicesSpeech, false);  // 排队，不打断台词
}

inline void Worker() {
    for(;;) {
        {
            std::unique_lock<std::mutex> lock(Mutex());
            Cv().wait_for(lock, std::chrono::milliseconds(50));
        }
        Tick();
    }
}

inline void EnsureWorker() {
    if(Started())
        return;
    Started() = true;
    std::thread(Worker).detach();
}

} // namespace detail

// ---------------- 对外 API ----------------

inline void SetEnabled(bool enabled) {
    detail::Enabled().store(enabled);
    if(!enabled) {
        detail::SkipRunning().store(false);
        TVPNarrationPlatform::StopSpeech();
        std::lock_guard<std::mutex> lock(detail::Mutex());
        detail::Lines().clear();
        detail::CurrentPage() = Page();
        detail::PageOpenFlag() = false;
        detail::PageFocusIndex() = -1;
        detail::SpokenDialogue().clear();
        detail::ChoiceFocus() = -1;
        detail::Choices().clear();
        detail::SpokenChoicesKey().clear();
    } else {
        detail::EnsureWorker();
    }
}

inline bool IsEnabled() { return detail::Enabled().load(); }

inline void SetConfig(unsigned flags) { detail::ConfigFlags() = flags; }
inline unsigned GetConfig() { return detail::ConfigFlags(); }

inline void SetScreenSize(int w, int h) {
    if(w > 0) detail::ScreenW() = w;
    if(h > 0) detail::ScreenH() = h;
}

// UI 层安装：激活第 index 项 / 把焦点视觉同步到第 index 项
inline void SetPageHandlers(detail::PageActivateFn activate, detail::PageFocusFn focus,
                            detail::PageAdjustFn adjust = nullptr) {
    detail::ActivateFn() = activate;
    detail::FocusFn() = focus;
    detail::AdjustFn() = adjust;
}

// 页面入栈：CPyMO「进页 → MENU 音效 + 报第一项」
inline void PageEntered(const Page &page) {
    if(!detail::Enabled().load(std::memory_order_relaxed))
        return;
    detail::EnsureWorker();
    std::string speech;
    int focus = -1;
    {
        std::lock_guard<std::mutex> lock(detail::Mutex());
        detail::CurrentPage() = page;
        detail::PageOpenFlag() = true;
        detail::PageFocusIndex() = detail::FirstFocus();
        speech = detail::PageEntrySpeech();
        focus = detail::PageFocusIndex();
    }
    detail::StateGen().fetch_add(1, std::memory_order_relaxed);
    detail::SoundAndVibrate(TVPNarrationPlatform::SOUND_MENU,
                           TVPNarrationPlatform::VIBRATE_LIGHT);
    if(!speech.empty())
        detail::Speak(speech, true);
    if(focus >= 0 && detail::FocusFn())
        detail::FocusFn()(focus);
}

// 页面内容变化（如勾选后值变了）：保持焦点，若焦点项措辞变化则重报
inline void PageUpdated(const Page &page) {
    if(!detail::Enabled().load(std::memory_order_relaxed) || !detail::PageOpenFlag())
        return;
    std::string before, after;
    {
        std::lock_guard<std::mutex> lock(detail::Mutex());
        before = detail::PageFocusSpeech();
        detail::CurrentPage() = page;
        if(detail::PageFocusIndex() >= (int) page.items.size() ||
           detail::PageFocusIndex() < 0)
            detail::PageFocusIndex() = detail::FirstFocus();
        after = detail::PageFocusSpeech();
    }
    detail::StateGen().fetch_add(1, std::memory_order_relaxed);
    if(!after.empty() && after != before)
        detail::Speak(after, true);
}

// 页面出栈
inline void PageClosed() {
    if(!detail::PageOpenFlag())
        return;
    {
        std::lock_guard<std::mutex> lock(detail::Mutex());
        detail::CurrentPage() = Page();
        detail::PageOpenFlag() = false;
        detail::PageFocusIndex() = -1;
        detail::StateGen().fetch_add(1, std::memory_order_relaxed);
        detail::SoundAndVibrate(TVPNarrationPlatform::SOUND_MENU,
                               TVPNarrationPlatform::VIBRATE_LIGHT);
    }
}

inline bool IsPageOpen() { return detail::PageOpenFlag(); }
inline int PageFocus() { return detail::PageFocusIndex(); }

// 图层文本（UTF-8）。layer 为图层标识，wx/wy/ww/wh 为窗口图像坐标
inline void NotifyLayerText(long layer, int wx, int wy, int ww, int wh,
                            const std::string &utf8) {
    if(!detail::Enabled().load(std::memory_order_relaxed) || utf8.empty())
        return;
    if(utf8.size() > detail::kMaxLineLen)
        return;
    detail::EnsureWorker();

    const unsigned now = detail::NowMs();
    bool newLine = false;
    std::string flushed;

    {
        std::lock_guard<std::mutex> lock(detail::Mutex());
        detail::LineState &st = detail::Lines()[layer];
        const bool same = (st.text == utf8);
        const bool grows = !st.text.empty() && utf8.size() > st.text.size() &&
                           utf8.compare(0, st.text.size(), st.text) == 0;
        if(!same) {
            if(grows) {
                st.text = utf8;  // 同一句继续打字
                st.tick = now;
                st.x = wx; st.y = wy; st.w = ww; st.h = wh;
            } else {
                // 换句：先把上一句没朗读的补说出去
                if(!st.spoken && !st.text.empty())
                    flushed = detail::OneLine(st.text);
                st.text = utf8;
                st.tick = now;
                st.spoken = false;
                st.x = wx; st.y = wy; st.w = ww; st.h = wh;
                newLine = true;
            }
        } else {
            st.tick = now;
        }
    }

    if(!flushed.empty())
        detail::Speak(flushed, true);
    if(newLine) {
        detail::StateGen().fetch_add(1, std::memory_order_relaxed);
        detail::SoundAndVibrate(TVPNarrationPlatform::SOUND_ENTER,
                               TVPNarrationPlatform::VIBRATE_LIGHT);
    }
    detail::Cv().notify_one();
}

// 图层销毁/清空
inline void ForgetLayer(long layer) {
    std::lock_guard<std::mutex> lock(detail::Mutex());
    detail::Lines().erase(layer);
}

// 无障碍动作（Android 手势 / 桌面快捷键 / 读屏键盘都走这里）
//---------------------------------------------------------------------------
// 剧情界面（没有引擎表单时）的焦点环：
//   第 0 项 = 台词本身（永远排在最前，确认键=推进对白）
//   第 1..n 项 = 剧情画面上除对话框外的其他可点按钮
// 按钮由 NarrationUi 从控件树里扫出来（脚本层画出来的图/按钮扫不到），这里只管导航。
//---------------------------------------------------------------------------
struct StoryItem {
    std::string label;  // 空 = 拿不到名字，项还在，只是不读
    int px;             // 中心坐标；-1 表示不移动游戏内鼠标
    int py;
    int space;          // 0 = 屏幕像素（引擎控件），1 = 窗口图像坐标（脚本图层按钮）
    int clickable;      // 1 = 可激活；0 = 只读文本（脚本画出来的文字）
};

namespace detail {
inline std::vector<TVPNarration::StoryItem> &StoryItems() {
    static std::vector<TVPNarration::StoryItem> v;
    return v;
}
inline int &StoryFocus() {
    static int i = -1;
    return i;
}
} // namespace detail

inline void SetStoryItems(const std::vector<StoryItem> &items) {
    std::lock_guard<std::mutex> lock(detail::Mutex());
    detail::StoryItems() = items;
    if(detail::StoryFocus() > (int) items.size())
        detail::StoryFocus() = -1;
}

// 当前剧情焦点（-1 = 不在焦点环里，0 = 台词）
inline int StoryFocus() { return detail::StoryFocus(); }

inline void DoAction(int action) {
    if(!detail::Enabled().load(std::memory_order_relaxed))
        return;

    switch(action) {
        case ACTION_UP:
        case ACTION_DOWN: {
            const int step = (action == ACTION_UP) ? -1 : 1;

            // 1) 页面：移动焦点 + SELECT + 报该项
            if(detail::PageOpenFlag()) {
                int focus = -1;
                std::string speech;
                {
                    std::lock_guard<std::mutex> lock(detail::Mutex());
                    focus = detail::NextFocus(detail::PageFocusIndex(), step);
                    detail::PageFocusIndex() = focus;
                    speech = detail::PageFocusSpeech();
                }
                if(focus >= 0) {
                    detail::SoundAndVibrate(TVPNarrationPlatform::SOUND_SELECT,
                                           TVPNarrationPlatform::VIBRATE_LIGHT);
                    if(!speech.empty())
                        detail::Speak(speech, true);
                    if(detail::FocusFn())
                        detail::FocusFn()(focus);
                }
                return;
            }

            // 2) 选项
            bool handled = false;
            std::string choiceSpeech;
            {
                std::lock_guard<std::mutex> lock(detail::Mutex());
                if(detail::Choices().size() >= 2) {
                    const int n = (int) detail::Choices().size();
                    if(detail::ChoiceFocus() < 0)
                        detail::ChoiceFocus() = (step > 0) ? 0 : n - 1;
                    else
                        detail::ChoiceFocus() = (detail::ChoiceFocus() + step + n) % n;
                    choiceSpeech = "选项 " + std::to_string(detail::ChoiceFocus() + 1) + "，" +
                                   detail::Choices()[detail::ChoiceFocus()].text;
                    handled = true;
                }
            }
            if(handled) {
                detail::SoundAndVibrate(TVPNarrationPlatform::SOUND_SELECT,
                                       TVPNarrationPlatform::VIBRATE_LIGHT);
                detail::Speak(choiceSpeech, true);
                return;
            }

            // 2.5) 剧情界面焦点环：台词是第 0 项，画面上的其他按钮依次跟上。
            // 越过第 0 项再往上/下就退出焦点环。
            {
                int next = -2;
                std::string label;
                int hoverX = -1, hoverY = -1, hoverSpace = 0;
                {
                    std::lock_guard<std::mutex> lock(detail::Mutex());
                    if(!detail::StoryItems().empty()) {
                        const int n = 1 + (int) detail::StoryItems().size();
                        int &f = detail::StoryFocus();
                        next = (f < 0) ? ((step > 0) ? 0 : n - 1) : (f + step);
                        if(next < 0) {
                            f = -1;  // 退出焦点环
                            next = -2;
                        } else {
                            if(next >= n)
                                next = 0;
                            f = next;
                            if(next == 0) {
                                label = detail::SpokenDialogue();
                            } else {
                                const StoryItem &it = detail::StoryItems()[next - 1];
                                label = it.label;
                                hoverX = it.px;
                                hoverY = it.py;
                                hoverSpace = it.space;
                            }
                        }
                    }
                }
                if(next >= 0) {
                    detail::SoundAndVibrate(TVPNarrationPlatform::SOUND_SELECT,
                                            TVPNarrationPlatform::VIBRATE_LIGHT);
                    if(!label.empty())
                        detail::Speak(label, true);
                    if(hoverX >= 0) {  // 焦点跟到按钮 ⇒ 游戏内鼠标也移过去
                        if(hoverSpace == 1)
                            NotifyMouseMove(hoverX, hoverY);  // 图像坐标
                        else
                            NotifyHoverPixels(hoverX, hoverY);  // 屏幕像素
                    }
                    return;
                }
            }

            // 历史不再由手势打开：游戏自己画的历史按钮会作为焦点项出现在环里，
            // 没有这样的按钮就代表这个游戏没有历史可看。
            return;
            return;
        }

        case ACTION_LEFT:
        case ACTION_RIGHT: {
            // 0) 页面：焦点项是滑条 → 调值，数值变化由页面刷新重报
            if(detail::PageOpenFlag() && detail::AdjustFn()) {
                int focus = -1;
                bool adjustable = false;
                {
                    std::lock_guard<std::mutex> lock(detail::Mutex());
                    focus = detail::PageFocusIndex();
                    if(focus >= 0 && focus < (int) detail::CurrentPage().items.size())
                        adjustable =
                            detail::CurrentPage().items[focus].kind == ITEM_SLIDER;
                }
                if(adjustable) {
                    detail::AdjustFn()(focus, action == ACTION_LEFT ? -1 : 1);
                    detail::SoundAndVibrate(TVPNarrationPlatform::SOUND_SELECT,
                                            TVPNarrationPlatform::VIBRATE_LIGHT);
                    return;
                }
            }

            bool handled = false;
            std::string choiceSpeech;
            int choiceX = -1, choiceY = -1;
            {
                std::lock_guard<std::mutex> lock(detail::Mutex());
                if(!detail::PageOpenFlag() && detail::Choices().size() >= 2 &&
                   detail::ChoiceFocus() >= 0) {
                    const int n = (int) detail::Choices().size();
                    const int step = (action == ACTION_LEFT) ? -1 : 1;
                    detail::ChoiceFocus() = (detail::ChoiceFocus() + step + n) % n;
                    const detail::Choice &c = detail::Choices()[detail::ChoiceFocus()];
                    choiceSpeech = "选项 " + std::to_string(detail::ChoiceFocus() + 1) + "，" +
                                   c.text;
                    choiceX = c.x + c.w / 2;
                    choiceY = c.y + c.h / 2;
                    handled = true;
                }
            }
            if(handled) {
                detail::Speak(choiceSpeech, true);
                if(choiceX >= 0)  // 游戏内鼠标跟到当前选项
                    NotifyMouseMove(choiceX, choiceY);
                return;
            }
            DoAction(action == ACTION_LEFT ? ACTION_UP : ACTION_DOWN);
            return;
        }

        case ACTION_OK: {
            // 1) 页面：ENTER + 激活焦点项
            if(detail::PageOpenFlag()) {
                int focus = -1;
                bool disabled = false;
                {
                    std::lock_guard<std::mutex> lock(detail::Mutex());
                    focus = detail::PageFocusIndex();
                    if(focus >= 0 && focus < (int) detail::CurrentPage().items.size())
                        disabled = detail::CurrentPage().items[focus].disabled;
                }
                if(disabled) {
                    detail::SoundAndVibrate(TVPNarrationPlatform::SOUND_SELECT,
                                           TVPNarrationPlatform::VIBRATE_MEDIUM);
                    detail::SpeakFeedback("不可用");
                    return;
                }
                if(focus >= 0 && detail::AdjustFn()) {
                    bool adjustable = false;
                    {
                        std::lock_guard<std::mutex> lock(detail::Mutex());
                        if(focus < (int) detail::CurrentPage().items.size())
                            adjustable = detail::CurrentPage().items[focus].kind ==
                                         ITEM_SLIDER;
                    }
                    if(adjustable) {
                        detail::AdjustFn()(focus, 1);
                        detail::SoundAndVibrate(TVPNarrationPlatform::SOUND_SELECT,
                                                TVPNarrationPlatform::VIBRATE_LIGHT);
                        return;
                    }
                }
                detail::SoundAndVibrate(TVPNarrationPlatform::SOUND_ENTER,
                                       TVPNarrationPlatform::VIBRATE_LIGHT);
                if(focus >= 0 && detail::ActivateFn())
                    detail::ActivateFn()(focus);
                return;
            }

            // 2) 选项：按该项图像坐标点击（KAG 选项只能走指针）
            int cx = -1, cy = -1;
            std::string text;
            {
                std::lock_guard<std::mutex> lock(detail::Mutex());
                if(detail::Choices().size() >= 2 && detail::ChoiceFocus() >= 0) {
                    const detail::Choice &c = detail::Choices()[detail::ChoiceFocus()];
                    cx = c.x + c.w / 2;
                    cy = c.y + c.h / 2;
                    text = c.text;
                }
            }
            detail::SoundAndVibrate(TVPNarrationPlatform::SOUND_ENTER,
                                   TVPNarrationPlatform::VIBRATE_LIGHT);
            if(cx >= 0) {
                TVPNarrationPlatform::TapImage(cx, cy);
                return;
            }

            // 2.5) 剧情界面焦点环：第 0 项是台词（下面按推进处理），其余是画面按钮
            {
                int storyFocus = -1;
                {
                    std::lock_guard<std::mutex> lock(detail::Mutex());
                    if(!detail::StoryItems().empty())
                        storyFocus = detail::StoryFocus();
                }
                if(storyFocus >= 1) {
                    detail::SoundAndVibrate(TVPNarrationPlatform::SOUND_ENTER,
                                            TVPNarrationPlatform::VIBRATE_LIGHT);
                    int space = 0, sx = 0, sy = 0;
                    {
                        std::lock_guard<std::mutex> lock(detail::Mutex());
                        if(storyFocus - 1 < (int) detail::StoryItems().size()) {
                            const StoryItem &it = detail::StoryItems()[storyFocus - 1];
                            space = it.space;
                            sx = it.px;
                            sy = it.py;
                        }
                    }
                    int clickable = 1;
                    {
                        std::lock_guard<std::mutex> lock(detail::Mutex());
                        if(storyFocus - 1 < (int) detail::StoryItems().size())
                            clickable = detail::StoryItems()[storyFocus - 1].clickable;
                    }
                    if(space == 1) {
                        NotifyTapImage(sx, sy);  // 脚本图层按钮：图像坐标注入一次左键
                    } else if(clickable) {
                        if(detail::ActivateFn())
                            detail::ActivateFn()(storyFocus);
                    } else {
                        // 只读文本（界面上的说明/标题/列表内容）：确认键就再读一遍
                        std::string again;
                        {
                            std::lock_guard<std::mutex> lock(detail::Mutex());
                            if(storyFocus - 1 < (int) detail::StoryItems().size())
                                again = detail::StoryItems()[storyFocus - 1].label;
                        }
                        if(!again.empty())
                            detail::Speak(again, true);
                    }
                    return;
                }
            }

            // 3) 推进对白
            // 安卓：原始触摸已经照常送进引擎（h.java → nativeTouches*），
            // 点一下本来就等于推进，这里再合成一次点击会让对白连跳两句，
            // 所以只在没有原始触摸通道的桌面端补这一下。
#if !defined(__ANDROID__)
            TVPNarrationPlatform::TapScreen(detail::ScreenW() / 2,
                                            static_cast<int>(detail::ScreenH() * 0.85f));
#endif
            return;
        }

        case ACTION_CANCEL: {
            // 长按：优先交给游戏自己的右键（右键菜单 / 隐藏对话框 / 跳过等）。
            // 游戏对右键毫无反应时，才退回「返回」。想直接取消请用双指下滑。
            if(detail::PageOpenFlag()) {
                detail::SoundAndVibrate(TVPNarrationPlatform::SOUND_MENU,
                                       TVPNarrationPlatform::VIBRATE_HEAVY);
                TVPNarrationPlatform::TapBack();
                return;
            }
            detail::SoundAndVibrate(TVPNarrationPlatform::SOUND_MENU,
                                   TVPNarrationPlatform::VIBRATE_HEAVY);
            detail::SkipRunning().store(false);
            {
                std::lock_guard<std::mutex> lock(detail::Mutex());
                detail::ChoiceFocus() = -1;
            }
            const unsigned gen = detail::StateGen().load();
            NotifyRightClick(-1, -1);
            std::thread([gen]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                // 半秒内页面/台词/选项毫无变化 ⇒ 这游戏不认右键，退回返回
                if(detail::StateGen().load() == gen &&
                   detail::Enabled().load(std::memory_order_relaxed))
                    TVPNarrationPlatform::TapBack();
            }).detach();
            return;
        }

        case ACTION_BACK: {
            // 显式取消：双指下滑。页面里退回上一层，剧情里退回游戏自己的返回。
            detail::SoundAndVibrate(TVPNarrationPlatform::SOUND_MENU,
                                   TVPNarrationPlatform::VIBRATE_HEAVY);
            if(!detail::PageOpenFlag()) {
                detail::SkipRunning().store(false);
                std::lock_guard<std::mutex> lock(detail::Mutex());
                detail::ChoiceFocus() = -1;
            }
            TVPNarrationPlatform::TapBack();
            return;
        }

        case ACTION_SCAN: {
            // 单击：CPyMO 的 onTap → onScan 只把指针移到触点（游戏已经收到真实触摸，
            // 点击推进/触发都由游戏自己处理），这里只补一次 hover，让游戏的光标和
            // 悬停效果跟到手指的位置。
            NotifyMouseMove(-1, -1);
            return;
        }

        case ACTION_SKIP:
            detail::SkipRunning().store(!detail::SkipRunning().load());
            detail::SoundAndVibrate(TVPNarrationPlatform::SOUND_SELECT,
                                   TVPNarrationPlatform::VIBRATE_MEDIUM);
            detail::SpeakFeedback(detail::SkipRunning().load() ? "开始跳过" : "停止跳过");
            return;

        case ACTION_SKIP_HOLD_START:
            detail::SkipRunning().store(true);
            detail::SoundAndVibrate(TVPNarrationPlatform::SOUND_SELECT,
                                   TVPNarrationPlatform::VIBRATE_MEDIUM);
            return;

        case ACTION_SKIP_HOLD_END:
            detail::SkipRunning().store(false);
            return;

        case ACTION_COPY:
        case ACTION_APPEND_COPY: {
            std::string text;
            {
                std::lock_guard<std::mutex> lock(detail::Mutex());
                text = detail::LastSpoken();
            }
            if(text.empty()) {
                detail::SpeakFeedback("没有可复制的文本");
                return;
            }
            const bool append = (action == ACTION_APPEND_COPY);
            if(append)
                TVPNarrationPlatform::CopyTextAppend(text);
            else
                TVPNarrationPlatform::CopyText(text);
            detail::SoundAndVibrate(TVPNarrationPlatform::SOUND_SELECT,
                                   TVPNarrationPlatform::VIBRATE_LIGHT);
            detail::SpeakFeedback(append ? "已追加复制" : "已复制");
            return;
        }

        case ACTION_REPEAT: {
            std::string text;
            {
                std::lock_guard<std::mutex> lock(detail::Mutex());
                if(detail::PageOpenFlag()) {
                    const int focus = detail::PageFocusIndex();
                    if(focus >= 0 && focus < (int) detail::CurrentPage().items.size())
                        text = detail::PageItemSpeech(detail::CurrentPage().items[focus]);
                } else if(detail::Choices().size() >= 2 && detail::ChoiceFocus() >= 0) {
                    text = detail::Choices()[detail::ChoiceFocus()].text;
                } else {
                    text = detail::SpokenDialogue();
                }
            }
            if(!text.empty())
                detail::Speak(text, true);
            else
                detail::SpeakFeedback("当前没有可朗读的文本");
            return;
        }

        default:
            return;
    }
}

inline void Shutdown() {
    detail::SkipRunning().store(false);
    TVPNarrationPlatform::StopSpeech();
}

} // namespace TVPNarration

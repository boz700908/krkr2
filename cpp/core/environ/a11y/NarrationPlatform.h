//---------------------------------------------------------------------------
// NarrationPlatform.h —— 朗读/反馈的平台后端选择
//
// 分层完全对照 CPyMO 的 cpymo_accessibility.h：
//   核心规则（NarrationCore.h）只负责「说什么、什么时候说」；
//   拿到最终文本后交给本层的 Speak()，各平台自己决定怎么发声。
//
//   Android : Java VisualHelper（TextToSpeech + SoundPool + Vibrator + Clipboard）
//   Windows : Tolk.dll（动态加载，缺省回退 SAPI）
//   macOS   : /usr/bin/say 常驻进程 + pbcopy
//   Linux   : spd-say（回退 espeak-ng/espeak）+ xclip/xsel/wl-copy
//
// 每个后端在 namespace TVPNarrationPlatform 中提供：
//   bool Speak(const std::string &utf8, bool flush);
//   void StopSpeech();
//   void PlaySound(int type);      // SOUND_ENTER/SOUND_MENU/SOUND_SELECT
//   void Vibrate(int milliseconds);
//   bool CopyText(const std::string &utf8);
//   bool CopyTextAppend(const std::string &utf8);
//   bool ClipboardAvailable();
//---------------------------------------------------------------------------
#pragma once

namespace TVPNarrationPlatform {

// 提示音类型（与 CPyMO / Android 一致）
enum SoundType {
    SOUND_ENTER = 1,  // 确认
    SOUND_MENU = 2,   // 取消 / 菜单
    SOUND_SELECT = 3  // 选择 / 上下移动
};

// 振动时长（毫秒，与 CPyMO / Android 一致）
enum VibrateDuration {
    VIBRATE_LIGHT = 10,   // 选择
    VIBRATE_MEDIUM = 20,  // 按住快进
    VIBRATE_HEAVY = 50    // 取消 / 长按
};

} // namespace TVPNarrationPlatform

#if defined(__ANDROID__)
#include "platform/NarrationPlatformAndroid.h"
#elif defined(_WIN32)
#include "platform/NarrationPlatformWin32.h"
#elif defined(__APPLE__)
#include "platform/NarrationPlatformApple.h"
#else
#include "platform/NarrationPlatformLinux.h"
#endif

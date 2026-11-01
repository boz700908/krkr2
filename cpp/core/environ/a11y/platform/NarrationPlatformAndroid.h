//---------------------------------------------------------------------------
// Android 后端：TTS / 音效 / 振动 / 剪贴板全部在 Java（对齐 CPyMO 的 VisualHelper）
// 需要宿主实现 org/tvp/kirikiri2/KR2Activity 的下列静态方法：
//   onNarrationSpeak(String text, boolean flush)
//   onNarrationStop()
//   onNarrationSound(int soundType)        1=确认 2=取消 3=选择
//   onNarrationVibrate(int milliseconds)   10 / 20 / 50
//   onNarrationCopy(String text, boolean append)
// 缺任一方法则本后端自动永久停用（旧宿主零开销）。
//---------------------------------------------------------------------------
#pragma once

#include <atomic>
#include <string>

#include "jni.h"
#include "platform/android/jni/JniHelper.h"

namespace TVPNarrationPlatform {

inline const char *JavaClass() { return "org/tvp/kirikiri2/KR2Activity"; }

inline std::atomic<bool> &JavaAvailable() {
    static std::atomic<bool> ok{true};
    return ok;
}

inline bool GetStatic(JniMethodInfo &mi, const char *name, const char *sig) {
    if(!JavaAvailable().load(std::memory_order_relaxed))
        return false;
    if(!cocos2d::JniHelper::getStaticMethodInfo(mi, JavaClass(), name, sig)) {
        JavaAvailable().store(false);
        return false;
    }
    return true;
}

inline jstring NewJString(JNIEnv *env, const std::string &utf8) {
    return env->NewStringUTF(utf8.c_str());
}

inline bool Speak(const std::string &utf8, bool flush) {
    if(utf8.empty())
        return false;
    JniMethodInfo mi;
    if(!GetStatic(mi, "onNarrationSpeak", "(Ljava/lang/String;Z)V"))
        return false;
    jstring js = NewJString(mi.env, utf8);
    if(js) {
        mi.env->CallStaticVoidMethod(mi.classID, mi.methodID, js, flush ? JNI_TRUE : JNI_FALSE);
        mi.env->DeleteLocalRef(js);
    }
    mi.env->DeleteLocalRef(mi.classID);
    return true;
}

inline void StopSpeech() {
    JniMethodInfo mi;
    if(!GetStatic(mi, "onNarrationStop", "()V"))
        return;
    mi.env->CallStaticVoidMethod(mi.classID, mi.methodID);
    mi.env->DeleteLocalRef(mi.classID);
}

inline void PlaySound(int type) {
    JniMethodInfo mi;
    if(!GetStatic(mi, "onNarrationSound", "(I)V"))
        return;
    mi.env->CallStaticVoidMethod(mi.classID, mi.methodID, static_cast<jint>(type));
    mi.env->DeleteLocalRef(mi.classID);
}

inline void Vibrate(int ms) {
    JniMethodInfo mi;
    if(!GetStatic(mi, "onNarrationVibrate", "(I)V"))
        return;
    mi.env->CallStaticVoidMethod(mi.classID, mi.methodID, static_cast<jint>(ms));
    mi.env->DeleteLocalRef(mi.classID);
}

inline bool CopyText(const std::string &utf8) {
    JniMethodInfo mi;
    if(!GetStatic(mi, "onNarrationCopy", "(Ljava/lang/String;Z)V"))
        return false;
    jstring js = NewJString(mi.env, utf8);
    if(js) {
        mi.env->CallStaticVoidMethod(mi.classID, mi.methodID, js, JNI_FALSE);
        mi.env->DeleteLocalRef(js);
    }
    mi.env->DeleteLocalRef(mi.classID);
    return true;
}

inline bool CopyTextAppend(const std::string &utf8) {
    JniMethodInfo mi;
    if(!GetStatic(mi, "onNarrationCopy", "(Ljava/lang/String;Z)V"))
        return false;
    jstring js = NewJString(mi.env, utf8);
    if(js) {
        mi.env->CallStaticVoidMethod(mi.classID, mi.methodID, js, JNI_TRUE);
        mi.env->DeleteLocalRef(js);
    }
    mi.env->DeleteLocalRef(mi.classID);
    return true;
}

inline bool ClipboardAvailable() { return JavaAvailable().load(std::memory_order_relaxed); }

// 点击注入：全部交给 Java 侧的 KR2Activity，避免核心层依赖 environ 的窗口符号
//   TapImage  选项：窗口图像坐标（走 nativeNarrationTap）
//   TapScreen 对白推进：屏幕像素（走 nativeTouchesBegin/End）
//   TapPixels 引擎 UI 控件：屏幕像素（同 TapScreen）
//   TapBack   返回键（走 nativeKeyAction(KEYCODE_BACK)）
inline void TapImage(int x, int y) {
    JniMethodInfo mi;
    if(!GetStatic(mi, "onNarrationTapImage", "(II)V"))
        return;
    mi.env->CallStaticVoidMethod(mi.classID, mi.methodID, static_cast<jint>(x), static_cast<jint>(y));
    mi.env->DeleteLocalRef(mi.classID);
}

inline void TapScreen(int x, int y) {
    JniMethodInfo mi;
    if(!GetStatic(mi, "onNarrationTap", "(II)V"))
        return;
    mi.env->CallStaticVoidMethod(mi.classID, mi.methodID, static_cast<jint>(x), static_cast<jint>(y));
    mi.env->DeleteLocalRef(mi.classID);
}

inline void TapPixels(int x, int y) { TapScreen(x, y); }

inline void TapBack() {
    JniMethodInfo mi;
    if(!GetStatic(mi, "onNarrationBack", "()V"))
        return;
    mi.env->CallStaticVoidMethod(mi.classID, mi.methodID);
    mi.env->DeleteLocalRef(mi.classID);
}

} // namespace TVPNarrationPlatform

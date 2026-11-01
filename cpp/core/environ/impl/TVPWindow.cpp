#include "TVPWindow.h"
#include <cocos2d.h>
#include <cocos-ext.h>

#include "cocos2d/MainScene.h"
#include "Application.h"
#include "DInputMgn.h"
#include "Random.h"
#include "TickCount.h"
#include "VelocityTracker.h"
#include "cocos2d/CCKeyCodeConv.h"
#include "ui/ConsoleWindow.h"
#include "Platform.h"
#include "RenderUtils.h"
#include "RenderManager.h"
#include "SystemImpl.h"
#include "vkdefine.h"

tTJSNI_Window *TVPWindowLayer::TVPGetActiveWindow() {
    if(!_currentWindowLayer)
        return nullptr;
    return _currentWindowLayer->getWindow();
}

TVPWindowLayer::~TVPWindowLayer() {
    if(_lastWindowLayer == this)
        _lastWindowLayer = _prevWindow;
    if(_nextWindow)
        _nextWindow->_prevWindow = _prevWindow;
    if(_prevWindow)
        _prevWindow->_nextWindow = _nextWindow;

    if(_currentWindowLayer == this) {
        TVPWindowLayer *anotherWin = _lastWindowLayer;
        while(anotherWin && !anotherWin->isVisible()) {
            anotherWin = anotherWin->_prevWindow;
        }
        if(anotherWin && anotherWin->isVisible()) {
            anotherWin->setPosition(0, 0);
            // anotherWin->setVisible(true);
        }
        _currentWindowLayer = anotherWin;
    }
}

bool TVPWindowLayer::init() {
    bool ret = inherit::init();
    setClippingToBounds(false);
    DrawSprite = cocos2d::Sprite::create();
    DrawSprite->setAnchorPoint(cocos2d::Vec2(0, 1)); // top-left
    PrimaryLayerArea = Node::create();
    addChild(PrimaryLayerArea);
    PrimaryLayerArea->addChild(DrawSprite);
    setAnchorPoint(cocos2d::Size::ZERO);
    cocos2d::EventListenerMouse *evmouse =
        cocos2d::EventListenerMouse::create();
    evmouse->onMouseScroll = [this](cocos2d::Event *e) { onMouseScroll(e); };
    evmouse->onMouseDown = [this](cocos2d::Event *e) { onMouseDownEvent(e); };
    evmouse->onMouseUp = [this](cocos2d::Event *e) { onMouseUpEvent(e); };
    evmouse->onMouseMove = [this](cocos2d::Event *e) { onMouseMoveEvent(e); };
    _eventDispatcher->addEventListenerWithSceneGraphPriority(evmouse, this);
    setTouchEnabled(false);
    setVisible(false);
    return ret;
}

void TVPWindowLayer::onMouseDownEvent(cocos2d::Event *_e) {
    auto *e = dynamic_cast<cocos2d::EventMouse *>(_e);
    switch(e->getMouseButton()) {
        case cocos2d::EventMouse::MouseButton::BUTTON_RIGHT:
            _mouseBtn = mbRight;
            onMouseDown(e->getLocation());
            break;
        case cocos2d::EventMouse::MouseButton::BUTTON_MIDDLE:
            _mouseBtn = mbMiddle;
            onMouseDown(e->getLocation());
            break;
        default:
            break;
    }
}

void TVPWindowLayer::onMouseUpEvent(cocos2d::Event *_e) {
    auto *e = dynamic_cast<cocos2d::EventMouse *>(_e);
    switch(e->getMouseButton()) {
        case cocos2d::EventMouse::MouseButton::BUTTON_RIGHT:
            _mouseBtn = mbRight;
            onMouseUp(e->getLocation());
            break;
        case cocos2d::EventMouse::MouseButton::BUTTON_MIDDLE:
            _mouseBtn = mbMiddle;
            onMouseUp(e->getLocation());
            break;
        default:
            break;
    }
}

void TVPWindowLayer::onMouseMoveEvent(cocos2d::Event *_e) {
    if(!_virutalMouseMode && _currentWindowLayer == this && !_touchMoved) {
        auto *e = dynamic_cast<cocos2d::EventMouse *>(_e);
        cocos2d::Vec2 pt(e->getCursorX(), e->getCursorY());
        onMouseMove(pt);
    }
}

void TVPWindowLayer::onMouseScroll(cocos2d::Event *_e) {
    auto *e = dynamic_cast<cocos2d::EventMouse *>(_e);
    if(!_windowMgrOverlay) {
        cocos2d::Vec2 nsp =
            PrimaryLayerArea->convertToNodeSpace(e->getLocation());
        int X = nsp.x, Y = PrimaryLayerArea->getContentSize().height - nsp.y;
        tjsNativeInstance->OnMouseWheel(TVPGetCurrentShiftKeyState(),
                                        e->getScrollY() > 0 ? -120 : 120, X, Y);
        return;
    }
    float scale = getZoomScale();
    if(e->getScrollY() > 0) {
        scale *= 0.9f;
    } else {
        scale *= 1.1f;
    }
    setZoomScale(scale);
    setContentOffset(getContentOffset());
    updateInset();
    relocateContainer(false);
}

bool TVPWindowLayer::onTouchBegan(cocos2d::Touch *touch,
                                  cocos2d::Event *unused_event) {
    if(_windowMgrOverlay)
        return inherit::onTouchBegan(touch, unused_event);
    if(std::find(_touches.begin(), _touches.end(), touch) == _touches.end()) {
        _touches.push_back(touch);
    }
    switch(_touches.size()) {
        case 1:
            _touchPoint = touch->getLocation();
            _touchMoved = false;
            _touchLength = 0.0f;
            _touchBeginTick = TVPGetRoughTickCount32();
            _mouseBtn = ::mbLeft;
            break;
        case 2:
            _mouseBtn = ::mbRight;
            _touchPoint = (_touchPoint + touch->getLocation()) / 2;
            break;
        case 3:
            _mouseBtn = ::mbMiddle;
            //_touchPoint = (_touchPoint + touch->getLocation()) /
            // 2;
        default:
            break;
    }
    return true;
}

void TVPWindowLayer::onTouchMoved(cocos2d::Touch *touch,
                                  cocos2d::Event *unused_event) {
    if(_windowMgrOverlay)
        return inherit::onTouchMoved(touch, unused_event);
    if(tjsNativeInstance) {
        if(_touches.size() == 1) {
            if(!_touchMoved &&
               (TVPGetRoughTickCount32() - _touchBeginTick > 150 ||
                _touchPoint.getDistanceSq(touch->getLocation()) >
                    _touchMoveThresholdSq)) {
                cocos2d::Vec2 nsp =
                    PrimaryLayerArea->convertToNodeSpace(_touchPoint);
                _LastMouseX = nsp.x,
                _LastMouseY = PrimaryLayerArea->getContentSize().height - nsp.y;
                TVPPostInputEvent(new tTVPOnMouseMoveInputEvent(
                    tjsNativeInstance, _LastMouseX, _LastMouseY,
                    TVPGetCurrentShiftKeyState()));
                _scancode[TVPConvertMouseBtnToVKCode(_mouseBtn)] = 0x11;
                TVPPostInputEvent(new tTVPOnMouseDownInputEvent(
                    tjsNativeInstance, _LastMouseX, _LastMouseY, _mouseBtn,
                    TVPGetCurrentShiftKeyState()));
                _touchMoved = true;
            } else if(_touchMoved) {
                cocos2d::Vec2 nsp =
                    PrimaryLayerArea->convertTouchToNodeSpace(touch);
                _LastMouseX = nsp.x,
                _LastMouseY = PrimaryLayerArea->getContentSize().height - nsp.y;
                TVPPostInputEvent(
                    new tTVPOnMouseMoveInputEvent(tjsNativeInstance,
                                                  _LastMouseX, _LastMouseY,
                                                  TVPGetCurrentShiftKeyState()),
                    TVP_EPT_DISCARDABLE);
                int pos = (_LastMouseY << 16) + _LastMouseX;
                TVPPushEnvironNoise(&pos, sizeof(pos));
            }
        }
    }
}

void TVPWindowLayer::onTouchEnded(cocos2d::Touch *touch,
                                  cocos2d::Event *unused_event) {
    if(_windowMgrOverlay)
        return inherit::onTouchEnded(touch, unused_event);
    auto touchIter = std::find(_touches.begin(), _touches.end(), touch);

    if(touchIter != _touches.end()) {
        if(_touches.size() == 1) {
            if(tjsNativeInstance) {
                cocos2d::Vec2 nsp =
                    PrimaryLayerArea->convertTouchToNodeSpace(touch);
                _LastMouseX = nsp.x,
                _LastMouseY = PrimaryLayerArea->getContentSize().height - nsp.y;
                if(!_touchMoved) {
                    TVPPostInputEvent(new tTVPOnMouseMoveInputEvent(
                        tjsNativeInstance, _LastMouseX, _LastMouseY,
                        TVPGetCurrentShiftKeyState()));
                    nsp = PrimaryLayerArea->convertToNodeSpace(_touchPoint);
                    TVPPostInputEvent(new tTVPOnMouseDownInputEvent(
                        tjsNativeInstance, nsp.x,
                        PrimaryLayerArea->getContentSize().height - nsp.y,
                        _mouseBtn, TVPGetCurrentShiftKeyState()));
                    TVPPostInputEvent(new tTVPOnClickInputEvent(
                        tjsNativeInstance, _LastMouseX, _LastMouseY));
                }
                _scancode[TVPConvertMouseBtnToVKCode(_mouseBtn)] = 0x10;
                TVPPostInputEvent(new tTVPOnMouseUpInputEvent(
                    tjsNativeInstance, _LastMouseX, _LastMouseY, _mouseBtn,
                    TVPGetCurrentShiftKeyState()));
            }
        }
        _touches.erase(touchIter);
    }

    if(_touches.size() == 0) {
        _dragging = false;
        _touchMoved = false;
        _scancode[TVPConvertMouseBtnToVKCode(_mouseBtn)] &= 0x10;
    }
}

void TVPWindowLayer::onTouchCancelled(cocos2d::Touch *touch,
                                      cocos2d::Event *unused_event) {
    if(_windowMgrOverlay)
        return inherit::onTouchCancelled(touch, unused_event);
    auto touchIter = std::find(_touches.begin(), _touches.end(), touch);
    if(touchIter != _touches.end()) {
        _touches.erase(touchIter);
    }

    if(_touches.size() == 0) {
        _dragging = false;
        _touchMoved = false;

        if(tjsNativeInstance) {
            cocos2d::Vec2 nsp =
                PrimaryLayerArea->convertTouchToNodeSpace(touch);
            _LastMouseX = nsp.x,
            _LastMouseY = PrimaryLayerArea->getContentSize().height - nsp.y;
            TVPPostInputEvent(new tTVPOnMouseUpInputEvent(
                tjsNativeInstance, _LastMouseX, _LastMouseY, _mouseBtn,
                TVPGetCurrentShiftKeyState()));
        }
    }
}

void TVPWindowLayer::onMouseDown(const cocos2d::Vec2 &pt) {
    cocos2d::Vec2 nsp = PrimaryLayerArea->convertToNodeSpace(pt);
    _LastMouseX = nsp.x,
    _LastMouseY = PrimaryLayerArea->getContentSize().height - nsp.y;
    _scancode[TVPConvertMouseBtnToVKCode(_mouseBtn)] = 0x11;
    TVPPostInputEvent(new tTVPOnMouseDownInputEvent(
        tjsNativeInstance, _LastMouseX, _LastMouseY, _mouseBtn,
        TVPGetCurrentShiftKeyState()));
}

void TVPWindowLayer::onMouseUp(const cocos2d::Vec2 &pt) {
    cocos2d::Vec2 nsp = PrimaryLayerArea->convertToNodeSpace(pt);
    _LastMouseX = nsp.x,
    _LastMouseY = PrimaryLayerArea->getContentSize().height - nsp.y;
    _scancode[TVPConvertMouseBtnToVKCode(_mouseBtn)] &= 0x10;
    TVPPostInputEvent(
        new tTVPOnMouseUpInputEvent(tjsNativeInstance, _LastMouseX, _LastMouseY,
                                    _mouseBtn, TVPGetCurrentShiftKeyState()));
}

void TVPWindowLayer::onMouseMove(const cocos2d::Vec2 &pt) {
    cocos2d::Vec2 nsp = PrimaryLayerArea->convertToNodeSpace(pt);
    _LastMouseX = nsp.x,
    _LastMouseY = PrimaryLayerArea->getContentSize().height - nsp.y;
    TVPPostInputEvent(new tTVPOnMouseMoveInputEvent(
                          tjsNativeInstance, _LastMouseX, _LastMouseY,
                          TVPGetCurrentShiftKeyState()),
                      TVP_EPT_DISCARDABLE);
    int pos = (_LastMouseY << 16) + _LastMouseX;
    TVPPushEnvironNoise(&pos, sizeof(pos));
}

void TVPWindowLayer::onMouseClick(const cocos2d::Vec2 &pt) {
    cocos2d::Vec2 nsp = PrimaryLayerArea->convertToNodeSpace(pt);
    _LastMouseX = nsp.x,
    _LastMouseY = PrimaryLayerArea->getContentSize().height - nsp.y;
    TVPPostInputEvent(new tTVPOnMouseMoveInputEvent(
                          tjsNativeInstance, _LastMouseX, _LastMouseY,
                          TVPGetCurrentShiftKeyState()),
                      TVP_EPT_DISCARDABLE);
    _scancode[TVPConvertMouseBtnToVKCode(_mouseBtn)] = 0x10;
    TVPPostInputEvent(new tTVPOnMouseDownInputEvent(
        tjsNativeInstance, _LastMouseX, _LastMouseY, _mouseBtn,
        TVPGetCurrentShiftKeyState()));
    TVPPostInputEvent(
        new tTVPOnClickInputEvent(tjsNativeInstance, _LastMouseX, _LastMouseY));
    TVPPostInputEvent(
        new tTVPOnMouseUpInputEvent(tjsNativeInstance, _LastMouseX, _LastMouseY,
                                    _mouseBtn, TVPGetCurrentShiftKeyState()));
}


void TVPWindowLayer::SetCursorPos(tjs_int x, tjs_int y) {
    cocos2d::Vec2 worldPt = PrimaryLayerArea->convertToWorldSpace(
        cocos2d::Vec2(x, PrimaryLayerArea->getContentSize().height - y));
    cocos2d::Vec2 pt = getParent()->convertToNodeSpace(worldPt);
    _LastMouseX = pt.x;
    _LastMouseY = pt.y;
    if(_mouseCursor) {
        _mouseCursor->setPosition(pt);
        _refadeMouseCursor();
    }
}

void TVPWindowLayer::SetImeMode(tTVPImeMode mode) {
    switch(mode) {
        case ::imDisable:
        case ::imClose:
#if CC_TARGET_PLATFORM == CC_PLATFORM_ANDROID
            TVPHideIME();
#else
            // #ifdef _MSC_VER
            TVPMainScene::GetInstance()->detachWithIME();
#endif
            break;
        case ::imOpen:
            // TVPMainScene::GetInstance()->attachWithIME();
            // break;
        default:
#if CC_TARGET_PLATFORM == CC_PLATFORM_ANDROID
        {
            cocos2d::Size screenSize = cocos2d::Director::getInstance()
                                           ->getOpenGLView()
                                           ->getFrameSize();
            TVPShowIME(0, _textInputPosY, screenSize.width,
                       screenSize.height / 4);
        }
#else
            // #ifdef _MSC_VER
            TVPMainScene::GetInstance()->attachWithIME();
#endif
        break;
    }
}

void TVPWindowLayer::ZoomRectangle(tjs_int &left, tjs_int &top, tjs_int &right,
                                   tjs_int &bottom) {
    left = static_cast<tjs_int64>(left) * ActualZoomNumer / ActualZoomDenom;
    top = static_cast<tjs_int64>(top) * ActualZoomNumer / ActualZoomDenom;
    right = static_cast<tjs_int64>(right) * ActualZoomNumer / ActualZoomDenom;
    bottom = static_cast<tjs_int64>(bottom) * ActualZoomNumer / ActualZoomDenom;
}

void TVPWindowLayer::BringToFront() {
    if(_currentWindowLayer != this) {
        if(_currentWindowLayer) {
            const cocos2d::Size &size = _currentWindowLayer->getViewSize();
            _currentWindowLayer->setPosition(cocos2d::Vec2(size.width, 0));
            _currentWindowLayer->tjsNativeInstance->OnReleaseCapture();
        }
        _currentWindowLayer = this;
    }
}

void TVPWindowLayer::ShowWindowAsModal() {
    in_mode_ = true;
    setVisible(true);
    BringToFront();
    if(_consoleWin) {
        _consoleWin->removeFromParent();
        _consoleWin = nullptr;
        TVPMainScene::GetInstance()->scheduleUpdate();

        cocos2d::Director::getInstance()->purgeCachedData();
        TVPControlAdDialog(0x10002, 0,
                           0); // ensure to close banner ad
    }
    modal_result_ = 0;
    while(this == _currentWindowLayer && !modal_result_) {
        int remain = TVPDrawSceneOnce(30); // 30 fps
        TVPProcessInputEvents(); // for iOS
        if(::Application->IsTarminate()) {
            modal_result_ = mrCancel;
        } else if(modal_result_ != 0) {
            break;
        } else if(remain > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(remain));
        }
    }
    in_mode_ = false;
}

void TVPWindowLayer::SetVisible(bool bVisible) {
    Visible = bVisible;
    setVisible(bVisible);
    if(bVisible) {
        BringToFront();
    } else {
        if(_currentWindowLayer == this) {
            _currentWindowLayer = _prevWindow ? _prevWindow : _nextWindow;
        }
    }
}

void TVPWindowLayer::ResetDrawSprite() const {
    if(DrawSprite) {
        cocos2d::Size size = getContentSize();
        float scale = static_cast<float>(ActualZoomNumer) / ActualZoomDenom;
        size = size / scale;
        DrawSprite->setScale(_drawTextureScaleX, _drawTextureScaleY);
        DrawSprite->setTextureRect(
            cocos2d::Rect(0, 0, LayerWidth, LayerHeight));
        DrawSprite->setPosition(cocos2d::Vec2(0, size.height));
        PrimaryLayerArea->setContentSize(size);
        PrimaryLayerArea->setScale(scale);
    }
}

void TVPWindowLayer::RecalcPaintBox() {
    if(!LayerWidth || !LayerHeight)
        return;
    ResetDrawSprite();
    cocos2d::Size size = getViewSize();
    cocos2d::Size contSize = getContentSize();
    float r = size.width / size.height;
    float R = contSize.width / contSize.height;
    float scale;
    cocos2d::Vec2 offset;
    if(R > r) {
        scale = size.width / contSize.width;
        offset.x = 0;
        offset.y = (size.height - contSize.height * scale) / 2;
    } else {
        scale = size.height / contSize.height;
        offset.x = (size.width - contSize.width * scale) / 2;
        offset.y = 0;
    }
    setMinScale(scale);
    setMaxScale(scale * 2);
    setZoomScale(scale);
    setContentOffset(offset);
    updateInset();
}

void TVPWindowLayer::SetWidth(tjs_int w) {
    cocos2d::Size size = getContentSize();
    size.width = w;
    setContentSize(size);
    RecalcPaintBox();
}

void TVPWindowLayer::SetHeight(tjs_int h) {
    cocos2d::Size size = getContentSize();
    size.height = h;
    setContentSize(size);
    RecalcPaintBox();
}

void TVPWindowLayer::UpdateDrawBuffer(iTVPTexture2D *tex) {
    if(!tex)
        return;
    cocos2d::Texture2D *tex2d = DrawSprite->getTexture();
    cocos2d::Texture2D *newtex = tex->GetAdapterTexture(tex2d);
    if(tex2d != newtex) {
        DrawSprite->setTexture(newtex);
        float sw, sh;
        tex->GetScale(_drawTextureScaleX, _drawTextureScaleY);
        if(_drawTextureScaleX == 1.f)
            sw = LayerWidth; // tex->GetWidth();
        else {
            sw = tex->GetInternalWidth() *
                (static_cast<float>(LayerWidth) / tex->GetWidth());
            _drawTextureScaleX = 1 / _drawTextureScaleX;
        }
        if(_drawTextureScaleY == 1.f)
            sh = LayerHeight; // tex->GetHeight();
        else {
            sh = tex->GetInternalHeight() *
                (static_cast<float>(LayerHeight) / tex->GetHeight());
            _drawTextureScaleY = 1 / _drawTextureScaleY;
        }
        DrawSprite->setTextureRect(cocos2d::Rect(0, 0, sw, sh));
        DrawSprite->setBlendFunc(cocos2d::BlendFunc::DISABLE);
        ResetDrawSprite();
    }
}

void TVPWindowLayer::toggleFillScale() {
    float scaleX = PrimaryLayerArea->getScaleX();
    float scaleY = PrimaryLayerArea->getScaleY();
    const cocos2d::Size &drawSize = PrimaryLayerArea->getContentSize();
    cocos2d::Size viewSize = getViewSize();
    float R = viewSize.width / viewSize.height;
    float r = drawSize.width / drawSize.height;
    if(fabs(R - r) < 0.01) {
        return; // do not fill border if screen ratio is almost
                // the same
    }

    if(scaleX == scaleY) {
        if(R > r) { // border @ left/right
            _drawSpriteScaleX = R / r;
            PrimaryLayerArea->setScaleX(scaleY * _drawSpriteScaleX);
        } else { // border @ top/bottom
            _drawSpriteScaleY = r / R;
            PrimaryLayerArea->setScaleY(scaleX * _drawSpriteScaleY);
        }
    } else {
        PrimaryLayerArea->setScale(std::min(scaleX, scaleY));
        _drawSpriteScaleX = 1.0f;
        _drawSpriteScaleY = 1.0f;
    }

    updateInset();
    setContentOffset(cocos2d::Vec2::ZERO);
    relocateContainer(false);
}

int TVPWindowLayer::GetMouseButtonState() const {
    int s = 0;
    if(TVPGetAsyncKeyState(VK_LBUTTON))
        s |= ssLeft;
    if(TVPGetAsyncKeyState(VK_RBUTTON))
        s |= ssRight;
    if(TVPGetAsyncKeyState(VK_MBUTTON))
        s |= ssMiddle;
    return s;
}

void TVPWindowLayer::OnMouseDown(tTVPMouseButton button, int shift, int x,
                                 int y) {
    // if (!CanSendPopupHide()) DeliverPopupHide();

    MouseVelocityTracker.addMovement(
        TVPGetRoughTickCount32(), static_cast<float>(x), static_cast<float>(y));

    LastMouseDownX = x;
    LastMouseDownY = y;

    if(tjsNativeInstance) {
        tjs_uint32 s = shift;
        s |= GetMouseButtonState();
        TVPPostInputEvent(
            new tTVPOnMouseDownInputEvent(tjsNativeInstance, x, y, button, s));
    }
}
void TVPWindowLayer::OnMouseClick() const {
    // fire click event
    if(tjsNativeInstance) {
        TVPPostInputEvent(new tTVPOnClickInputEvent(
            tjsNativeInstance, LastMouseDownX, LastMouseDownY));
    }
}

void TVPWindowLayer::GenerateMouseEvent(bool fl, bool fr, bool fu, bool fd) {
    if(!fl && !fr && !fu && !fd) {
        if(TVPGetRoughTickCount32() - 45 < LastMouseKeyTick)
            return;
    }

    bool shift = 0 != (TVPGetKeyMouseAsyncState(VK_SHIFT, true));
    bool left = fl || TVPGetKeyMouseAsyncState(VK_LEFT, true) ||
        TVPGetJoyPadAsyncState(VK_PADLEFT, true);
    bool right = fr || TVPGetKeyMouseAsyncState(VK_RIGHT, true) ||
        TVPGetJoyPadAsyncState(VK_PADRIGHT, true);
    bool up = fu || TVPGetKeyMouseAsyncState(VK_UP, true) ||
        TVPGetJoyPadAsyncState(VK_PADUP, true);
    bool down = fd || TVPGetKeyMouseAsyncState(VK_DOWN, true) ||
        TVPGetJoyPadAsyncState(VK_PADDOWN, true);

    uint32_t flags = 0;
    if(left || right || up || down)
        flags |= /*MOUSEEVENTF_MOVE*/ 1;

    if(!right && !left && !up && !down) {
        LastMouseMoved = false;
        MouseKeyXAccel = MouseKeyYAccel = 0;
    }

    if(!shift) {
        if(!right && left && MouseKeyXAccel > 0)
            MouseKeyXAccel = -0;
        if(!left && right && MouseKeyXAccel < 0)
            MouseKeyXAccel = 0;
        if(!down && up && MouseKeyYAccel > 0)
            MouseKeyYAccel = -0;
        if(!up && down && MouseKeyYAccel < 0)
            MouseKeyYAccel = 0;
    } else {
        if(left)
            MouseKeyXAccel = -TVP_MOUSE_SHIFT_ACCEL;
        if(right)
            MouseKeyXAccel = TVP_MOUSE_SHIFT_ACCEL;
        if(up)
            MouseKeyYAccel = -TVP_MOUSE_SHIFT_ACCEL;
        if(down)
            MouseKeyYAccel = TVP_MOUSE_SHIFT_ACCEL;
    }

    if(right || left || up || down) {
        if(left)
            if(MouseKeyXAccel > -TVP_MOUSE_MAX_ACCEL)
                MouseKeyXAccel = MouseKeyXAccel ? MouseKeyXAccel - 2 : -2;
        if(right)
            if(MouseKeyXAccel < TVP_MOUSE_MAX_ACCEL)
                MouseKeyXAccel = MouseKeyXAccel ? MouseKeyXAccel + 2 : +2;
        if(!left && !right) {
            if(MouseKeyXAccel > 0)
                MouseKeyXAccel--;
            else if(MouseKeyXAccel < 0)
                MouseKeyXAccel++;
        }

        if(up)
            if(MouseKeyYAccel > -TVP_MOUSE_MAX_ACCEL)
                MouseKeyYAccel = MouseKeyYAccel ? MouseKeyYAccel - 2 : -2;
        if(down)
            if(MouseKeyYAccel < TVP_MOUSE_MAX_ACCEL)
                MouseKeyYAccel = MouseKeyYAccel ? MouseKeyYAccel + 2 : +2;
        if(!up && !down) {
            if(MouseKeyYAccel > 0)
                MouseKeyYAccel--;
            else if(MouseKeyYAccel < 0)
                MouseKeyYAccel++;
        }
    }

    if(flags) {
        _LastMouseX += MouseKeyXAccel >> 1;
        _LastMouseY += MouseKeyYAccel >> 1;
        LastMouseMoved = true;
    }
    LastMouseKeyTick = TVPGetRoughTickCount32();
}

void TVPWindowLayer::InternalKeyDown(tjs_uint16 key, tjs_uint32 shift) {
    tjs_uint32 tick = TVPGetRoughTickCount32();
    TVPPushEnvironNoise(&tick, sizeof(tick));
    TVPPushEnvironNoise(&key, sizeof(key));
    TVPPushEnvironNoise(&shift, sizeof(shift));

    if(UseMouseKey) {
        if(key == VK_RETURN || key == VK_SPACE || key == VK_ESCAPE ||
           key == VK_PAD1 || key == VK_PAD2) {
            cocos2d::Vec2 p(_LastMouseX, _LastMouseY);
            cocos2d::Size size = PrimaryLayerArea->getContentSize();
            if(p.x >= 0 && p.y >= 0 && p.x < size.width && p.y < size.height) {
                if(key == VK_RETURN || key == VK_SPACE || key == VK_PAD1) {
                    MouseLeftButtonEmulatedPushed = true;
                    OnMouseDown(mbLeft, 0, p.x, p.y);
                }

                if(key == VK_ESCAPE || key == VK_PAD2) {
                    MouseRightButtonEmulatedPushed = true;
                    OnMouseDown(mbLeft, 0, p.x, p.y);
                }
            }
            return;
        }

        switch(key) {
            case VK_LEFT:
            case VK_PADLEFT:
                if(MouseKeyXAccel == 0 && MouseKeyYAccel == 0) {
                    GenerateMouseEvent(true, false, false, false);
                    LastMouseKeyTick = TVPGetRoughTickCount32() + 100;
                }
                return;
            case VK_RIGHT:
            case VK_PADRIGHT:
                if(MouseKeyXAccel == 0 && MouseKeyYAccel == 0) {
                    GenerateMouseEvent(false, true, false, false);
                    LastMouseKeyTick = TVPGetRoughTickCount32() + 100;
                }
                return;
            case VK_UP:
            case VK_PADUP:
                if(MouseKeyXAccel == 0 && MouseKeyYAccel == 0) {
                    GenerateMouseEvent(false, false, true, false);
                    LastMouseKeyTick = TVPGetRoughTickCount32() + 100;
                }
                return;
            case VK_DOWN:
            case VK_PADDOWN:
                if(MouseKeyXAccel == 0 && MouseKeyYAccel == 0) {
                    GenerateMouseEvent(false, false, false, true);
                    LastMouseKeyTick = TVPGetRoughTickCount32() + 100;
                }
                return;
            default:;
        }
    }
    TVPPostInputEvent(
        new tTVPOnKeyDownInputEvent(tjsNativeInstance, key, shift));
}

void TVPWindowLayer::InternalKeyUp(tjs_uint16 key, tjs_uint32 shift) {
    tjs_uint32 tick = TVPGetRoughTickCount32();
    TVPPushEnvironNoise(&tick, sizeof(tick));
    TVPPushEnvironNoise(&key, sizeof(key));
    TVPPushEnvironNoise(&shift, sizeof(shift));
    if(tjsNativeInstance) {
        if(UseMouseKey /*&& PaintBox*/) {
            if(key == VK_RETURN || key == VK_SPACE || key == VK_ESCAPE ||
               key == VK_PAD1 || key == VK_PAD2) {
                cocos2d::Vec2 p(_LastMouseX, _LastMouseY);
                cocos2d::Size size = PrimaryLayerArea->getContentSize();
                if(p.x >= 0 && p.y >= 0 && p.x < size.width &&
                   p.y < size.height) {
                    if(key == VK_RETURN || key == VK_SPACE || key == VK_PAD1) {
                        OnMouseClick();
                        MouseLeftButtonEmulatedPushed = false;
                        OnMouseUp(mbLeft, 0, p.x, p.y);
                    }

                    if(key == VK_ESCAPE || key == VK_PAD2) {
                        MouseRightButtonEmulatedPushed = false;
                        OnMouseUp(mbRight, 0, p.x, p.y);
                    }
                }
                return;
            }
        }

        TVPPostInputEvent(
            new tTVPOnKeyUpInputEvent(tjsNativeInstance, key, shift));
    }
}

void TVPWindowLayer::OnClose(CloseAction &action) {
    if(modal_result_ == 0)
        action = caNone;
    else
        action = caHide;

    if(ProgramClosing) {
        if(tjsNativeInstance) {
            if(tjsNativeInstance->IsMainWindow()) {
                // this is the main window
            } else {
                // not the main window
                action = caFree;
            }
            // if (TVPFullScreenedWindow != this) {
            //  if this is not a fullscreened window
            //	SetVisible(false);
            // }
            iTJSDispatch2 *obj = tjsNativeInstance->GetOwnerNoAddRef();
            tjsNativeInstance->NotifyWindowClose();
            obj->Invalidate(0, nullptr, nullptr, obj);
            tjsNativeInstance = nullptr;
            SetVisible(false);
            scheduleOnce([this](float) { removeFromParent(); }, 0, "remove");
        }
    }
}

bool TVPWindowLayer::OnCloseQuery() {
    // closing actions are 3 patterns;
    // 1. closing action by the user
    // 2. "close" method
    // 3. object invalidation

    if(TVPGetBreathing()) {
        return false;
    }

    // the default event handler will invalidate this object when
    // an onCloseQuery event reaches the handler.
    if(tjsNativeInstance && (modal_result_ == 0 || modal_result_ == mrCancel /* mrCancel=when close button is pushed in modal window */)) {
        if(iTJSDispatch2 *obj = tjsNativeInstance->GetOwnerNoAddRef()) {
            tTJSVariant arg[1] = { true };
            static ttstr eventname(TJS_W("onCloseQuery"));

            if(!ProgramClosing) {
                // close action does not happen immediately
                if(tjsNativeInstance) {
                    TVPPostInputEvent(
                        new tTVPOnCloseInputEvent(tjsNativeInstance));
                }

                Closing = true; // waiting closing...
                //	TVPSystemControl->NotifyCloseClicked();
                return false;
            } else {
                CanCloseWork = true;
                TVPPostEvent(obj, obj, eventname, 0, TVP_EPT_IMMEDIATE, 1, arg);
                TVPDrawSceneOnce(0); // for post event
                // this event happens immediately
                // and does not return until done
                return CanCloseWork; // CanCloseWork is set by the
                                     // event handler
            }
        } else {
            return true;
        }
    } else {
        return true;
    }
}

void TVPWindowLayer::Close() {
    // closing action by "close" method
    if(Closing)
        return; // already waiting closing...

    ProgramClosing = true;
    try {
        // tTVPWindow::Close();
        if(in_mode_) {
            modal_result_ = mrCancel;
        } else if(OnCloseQuery()) {
            CloseAction action = caFree;
            OnClose(action);
            switch(action) {
                case caNone:
                    break;
                case caHide:
                    cocos2d::Hide();
                    break;
                case caMinimize:
                    //::ShowWindow(GetHandle(), SW_MINIMIZE);
                    break;
                case caFree:
                default:
                    scheduleOnce([this](float) { removeFromParent(); }, 0,
                                 "Close");
                    //::PostMessage(GetHandle(),
                    //: TVP_EV_WINDOW_RELEASE, 0, 0);
                    break;
            }
        }
    } catch(...) {
        ProgramClosing = false;
        throw;
    }
    ProgramClosing = false;
}

void TVPWindowLayer::OnCloseQueryCalled(bool b) {
    // closing is allowed by onCloseQuery event handler
    if(!ProgramClosing) {
        // closing action by the user
        if(b) {
            if(in_mode_)
                modal_result_ = 1; // when modal
            else
                SetVisible(false); // just hide

            Closing = false;
            if(tjsNativeInstance) {
                if(tjsNativeInstance->IsMainWindow()) {
                    // this is the main window
                    iTJSDispatch2 *obj = tjsNativeInstance->GetOwnerNoAddRef();
                    obj->Invalidate(0, nullptr, nullptr, obj);
                    // TJSNativeInstance = nullptr; //
                    // ���ζ��A�ǤϼȤ�this����������Ƥ��뤿�ᡢ���Щ`�إ����������ƤϤ����ʤ�
                }
            } else {
                delete this;
            }
        } else {
            Closing = false;
        }
    } else {
        // closing action by the program
        CanCloseWork = b;
    }
}

void TVPWindowLayer::SetUseMouseKey(bool b) {
    UseMouseKey = b;
    if(b) {
        MouseLeftButtonEmulatedPushed = false;
        MouseRightButtonEmulatedPushed = false;
        LastMouseKeyTick = TVPGetRoughTickCount32();
    } else {
        if(MouseLeftButtonEmulatedPushed) {
            MouseLeftButtonEmulatedPushed = false;
            OnMouseUp(mbLeft, 0, _LastMouseX, _LastMouseY);
        }
        if(MouseRightButtonEmulatedPushed) {
            MouseRightButtonEmulatedPushed = false;
            OnMouseUp(mbRight, 0, _LastMouseX, _LastMouseY);
        }
    }
}

void TVPWindowLayer::OnMouseUp(tTVPMouseButton button, int shift, int x,
                               int y) {
    MouseVelocityTracker.addMovement(
        TVPGetRoughTickCount32(), static_cast<float>(x), static_cast<float>(y));
    if(tjsNativeInstance) {
        tjs_uint32 s = shift;
        s |= GetMouseButtonState();
        TVPPostInputEvent(
            new tTVPOnMouseUpInputEvent(tjsNativeInstance, x, y, button, s));
    }
}

void TVPWindowLayer::OnKeyPress(tjs_uint16 vk, int repeat, bool prevkeystate,
                                bool convertkey) {
    if(tjsNativeInstance && vk) {
        if(UseMouseKey && (vk == 0x1b || vk == 13 || vk == 32))
            return;
        TVPPostInputEvent(new tTVPOnKeyPressInputEvent(tjsNativeInstance, vk));
    }
}
#include "../a11y/NarrationBridge.h"

namespace {
// 尽早把 visual 的朗读钩子接到规则层（此处是 environ，能拿到平台后端）
// 无障碍：问游戏要它当前的光标位置（图像坐标）。走 iWindowLayer::GetCursorPos，
// 不去碰 TVPWindowLayer 的私有字段。
static bool TVPNarrationCursorImagePos(int &x, int &y) {
    TVPWindowLayer *lay = TVPWindowLayer::_currentWindowLayer;
    if(!lay)
        return false;
    tjs_int cx = 0, cy = 0;
    lay->GetCursorPos(cx, cy);
    x = (int) cx;
    y = (int) cy;
    return true;
}

// 无障碍：把游戏内鼠标移到指定窗口图像坐标（只移动、不点击）
static void TVPNarrationMoveMouseImpl(int x, int y) {
    tTJSNI_Window *win = TVPWindowLayer::TVPGetActiveWindow();
    if(!win)
        return;
    if(x < 0 || y < 0) {
        // 负坐标：用游戏自己记录的光标位置（图像坐标），即「移到手指刚点的位置」
        if(!TVPNarrationCursorImagePos(x, y))
            return;
    }
    TVPPostInputEvent(new tTVPOnMouseMoveInputEvent(win, x, y,
                                                    TVPGetCurrentShiftKeyState()),
                      TVP_EPT_DISCARDABLE);
}

// 无障碍：屏幕像素 → 游戏内鼠标。界面/控件坐标是像素，引擎鼠标事件要图像坐标，
// 这里按 GLView 视口与图层缩放比换算（与 SetCursorPos 互为逆运算）。
static void TVPNarrationHoverPixelsImpl(int px, int py) {
    TVPWindowLayer *lay = TVPWindowLayer::_currentWindowLayer;
    if(!lay)
        return;
    cocos2d::Node *area = lay->GetPrimaryArea();
    if(!area)
        return;
    cocos2d::GLView *glview = cocos2d::Director::getInstance()->getOpenGLView();
    if(!glview)
        return;
    const float sx = glview->getScaleX();
    const float sy = glview->getScaleY();
    if(sx == 0.0f || sy == 0.0f)
        return;
    const cocos2d::Rect vp = glview->getViewPortRect();
    const cocos2d::Size frame = glview->getFrameSize();
    const cocos2d::Vec2 world((px - vp.origin.x) / sx,
                              (frame.height - py - vp.origin.y) / sy);
    const cocos2d::Vec2 local = area->convertToNodeSpace(world);
    const int ix = (int) local.x;
    const int iy = (int) (area->getContentSize().height - local.y);
    TVPNarrationMoveMouseImpl(ix, iy);
}

// 无障碍：在游戏内发一次真正的右键（长按手势 → 游戏自己的右键菜单）
static void TVPNarrationRightClickImpl(int x, int y) {
    tTJSNI_Window *win = TVPWindowLayer::TVPGetActiveWindow();
    if(!win)
        return;
    const tjs_uint32 shift = TVPGetCurrentShiftKeyState();
    if(x < 0 || y < 0) {
        if(!TVPNarrationCursorImagePos(x, y))
            return;
    }
    TVPPostInputEvent(new tTVPOnMouseMoveInputEvent(win, x, y, shift));
    TVPPostInputEvent(new tTVPOnMouseDownInputEvent(win, x, y, ::mbRight, shift));
    TVPPostInputEvent(new tTVPOnMouseUpInputEvent(win, x, y, ::mbRight, shift));
}

// 无障碍：在窗口图像坐标处注入一次左键按下 + 抬起。
// 脚本图层画的按钮只认这条通道（它们没有 cocos 控件），按下/抬起与真实点击同路。
static void TVPNarrationTapImageImpl(int x, int y) {
    tTJSNI_Window *win = TVPWindowLayer::TVPGetActiveWindow();
    if(!win)
        return;
    const tjs_uint32 shift = TVPGetCurrentShiftKeyState();
    if(x < 0 || y < 0) {
        if(!TVPNarrationCursorImagePos(x, y))
            return;
    }
    TVPPostInputEvent(new tTVPOnMouseMoveInputEvent(win, x, y, shift));
    TVPPostInputEvent(new tTVPOnMouseDownInputEvent(win, x, y, ::mbLeft, shift));
    TVPPostInputEvent(new tTVPOnMouseUpInputEvent(win, x, y, ::mbLeft, shift));
}

// 无障碍：枚举脚本层里「图片画出来的按钮」。
// KAG 的按钮就是带事件的 Layer，名字和矩形引擎里本来就有，不需要 OCR。
// 判据（全部立刻可验证，不猜）：
//   ① 可见、有图像、有内部名字；
//   ② 不是全屏层（全屏的多半是背景层/消息层/推进层，那里点击没有独立含义）；
//   ③ 在它自己的中心点上，最前面的图层就是它（否则点击会被上层吃掉，读了也没用）。
static void TVPNarrationCollectLayerButtonsImpl(std::vector<TVPNarration::LayerButton> &out) {
    tTJSNI_Window *win = TVPWindowLayer::TVPGetActiveWindow();
    if(!win)
        return;
    iTVPDrawDevice *dev = win->GetDrawDevice();
    if(!dev)
        return;
    tTJSNI_BaseLayer *primary = dev->GetPrimaryLayer();
    if(!primary)
        return;

    const tTVPRect area = primary->GetRect();
    const tjs_int pw = area.get_width() > 0 ? area.get_width() : 1;
    const tjs_int ph = area.get_height() > 0 ? area.get_height() : 1;

    struct Collector {
        std::vector<TVPNarration::LayerButton> *out;
        tTJSNI_BaseLayer *primary;
        tjs_int pw, ph;

        void Walk(tTJSNI_BaseLayer *parent, int depth) {
            if(!parent || depth > 8)
                return;
            const tjs_uint n = parent->GetCount();
            for(tjs_uint i = 0; i < n; ++i) {
                tTJSNI_BaseLayer *child = parent->GetChildren((tjs_int) i);
                Consider(child);
                Walk(child, depth + 1);
            }
        }

        void Consider(tTJSNI_BaseLayer *layer) {
            if(!layer || !layer->GetVisible() || !layer->GetParentVisible())
                return;
            if(!layer->GetHasImage())
                return;
            const tTVPRect rect = layer->GetRect();
            const tjs_int w = rect.get_width(), h = rect.get_height();
            if(w < 6 || h < 6)
                return;
            if((int64_t) w * h > (int64_t) pw * ph * 9 / 10)
                return;  // 全屏层不是按钮
            const ttstr &name = layer->GetName();
            const tjs_char *nameText = name.c_str();
            tjs_int cx = w / 2, cy = h / 2;
            layer->ToPrimaryCoordinates(cx, cy);
            if(cx < 0 || cy < 0 || cx >= pw || cy >= ph)
                return;
            tTJSNI_BaseLayer *front = primary->GetMostFrontChildAt(cx, cy);
            bool reachable = false;
            for(tTJSNI_BaseLayer *cur = front; cur; cur = cur->GetParent())
                if(cur == layer) {
                    reachable = true;
                    break;
                }
            if(!reachable)
                return;  // 点击到不了它

            size_t length = 0;
            while(nameText && nameText[length])
                ++length;
            TVPNarration::LayerButton button;
            // 没有内部名字也收：无障碍层会保留这一项（能聚焦、能点），只是不读它，
            // 绝不编造名字。
            button.label = length ? TVPNarration::ToUtf8(nameText, length) : std::string();
            button.x = (int) cx;
            button.y = (int) cy;
            out->push_back(button);
        }
    };

    Collector collector = { &out, primary, pw, ph };
    collector.Walk(primary, 0);
}

struct TVPNarrationInstaller {
    TVPNarrationInstaller() {
        TVPNarration::InstallLayerTextSink();
        TVPNarration::SetMouseMoveHandler(&TVPNarrationMoveMouseImpl);
        TVPNarration::SetHoverPixelsHandler(&TVPNarrationHoverPixelsImpl);
        TVPNarration::SetRightClickHandler(&TVPNarrationRightClickImpl);
        TVPNarration::SetTapImageHandler(&TVPNarrationTapImageImpl);
        TVPNarration::SetCollectLayerButtonsHandler(&TVPNarrationCollectLayerButtonsImpl);
    }
} g_tvpNarrationInstaller;
} // namespace

// 无障碍：Java 侧「确认」手势以窗口图像坐标合成一次左键点击，
// 交给游戏脚本（KAG 选项层）自己命中选择。
// 由 platforms/android/cpp/krkr2_android.cpp 的 nativeNarrationTap 调用。
void TVPNarrationActivateImageAt(int x, int y) {
    tTJSNI_Window *win = TVPWindowLayer::TVPGetActiveWindow();
    if(!win)
        return;
    const tjs_uint32 shift = TVPGetCurrentShiftKeyState();
    TVPPostInputEvent(new tTVPOnMouseDownInputEvent(win, x, y, ::mbLeft, shift));
    TVPPostInputEvent(new tTVPOnMouseUpInputEvent(win, x, y, ::mbLeft, shift));
}

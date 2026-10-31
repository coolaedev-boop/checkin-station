#pragma once
// ============================================================================
//  Chrome: the parts of the design that stay put while screens change.
//  Background: white page, the red top/bottom borders, AVTF and NCCA logos.
//  Overlay: session bar (Name/Class/Project), status icons, nav bar, toasts.
// ============================================================================
#include "Screens.h"
#include <string>

namespace Chrome {
void buildBackground();               // first, so the screens draw over it
void buildOverlay();                  // after the screens, so it draws over them
void layout(ScreenId shown);          // which parts the shown screen uses
void refreshSession();                // the session bar's text
void refreshStatus();                 // Bluetooth / Wi-Fi / server icons, 1 Hz
void toast(const std::string& text, bool warning = false);   // short message at the top, fades by itself
void hideToast();                     // screen changed: the message was about the old one
}

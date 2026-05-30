#pragma once

#include <notcurses/nckeys.h>
#include <notcurses/notcurses.h>

#include "tools/logger.hpp"

#include <format>

namespace Tools::NCPP {

static inline void log_keypress(ncinput *ni) {
  // clang-format off
      Tools::Log::Debug(std::format("UTF8: {}", ni->utf8), "KEY-PRESSED");
      Tools::Log::Debug(std::format("BUILTIN SHIFT: {}", ni->shift), "KEY-PRESSED");
      Tools::Log::Debug(std::format("BUILTIN CTRL: {}", ni->ctrl), "KEY-PRESSED");
      Tools::Log::Debug(std::format("BUILTIN ALT: {}", ni->alt), "KEY-PRESSED");
      Tools::Log::Debug(std::format("EFF. TEXT: {}", ni->eff_text), "KEY-PRESSED");
      Tools::Log::Debug(std::format("PRED. CTRL: {}", ncinput_ctrl_p(ni)), "KEY-PRESSED");
      Tools::Log::Debug(std::format("PRED. SHIFT: {}", ncinput_shift_p(ni)), "KEY-PRESSED");
      Tools::Log::Debug(std::format("PRED. ALT: {}", ncinput_alt_p(ni)), "KEY-PRESSED");
  // clang-format on
}

static inline const char *nckeystr(char32_t spkey) {
  switch (spkey) { // FIXME
  case NCKEY_RESIZE:
    return "resize event";
  case NCKEY_INVALID:
    return "invalid";
  case NCKEY_LEFT:
    return "left";
  case NCKEY_UP:
    return "up";
  case NCKEY_RIGHT:
    return "right";
  case NCKEY_DOWN:
    return "down";
  case NCKEY_INS:
    return "insert";
  case NCKEY_DEL:
    return "delete";
  case NCKEY_PGDOWN:
    return "pgdown";
  case NCKEY_PGUP:
    return "pgup";
  case NCKEY_HOME:
    return "home";
  case NCKEY_END:
    return "end";
  case NCKEY_F00:
    return "F0";
  case NCKEY_F01:
    return "F1";
  case NCKEY_F02:
    return "F2";
  case NCKEY_F03:
    return "F3";
  case NCKEY_F04:
    return "F4";
  case NCKEY_F05:
    return "F5";
  case NCKEY_F06:
    return "F6";
  case NCKEY_F07:
    return "F7";
  case NCKEY_F08:
    return "F8";
  case NCKEY_F09:
    return "F9";
  case NCKEY_F10:
    return "F10";
  case NCKEY_F11:
    return "F11";
  case NCKEY_F12:
    return "F12";
  case NCKEY_F13:
    return "F13";
  case NCKEY_F14:
    return "F14";
  case NCKEY_F15:
    return "F15";
  case NCKEY_F16:
    return "F16";
  case NCKEY_F17:
    return "F17";
  case NCKEY_F18:
    return "F18";
  case NCKEY_F19:
    return "F19";
  case NCKEY_F20:
    return "F20";
  case NCKEY_F21:
    return "F21";
  case NCKEY_F22:
    return "F22";
  case NCKEY_F23:
    return "F23";
  case NCKEY_F24:
    return "F24";
  case NCKEY_F25:
    return "F25";
  case NCKEY_F26:
    return "F26";
  case NCKEY_F27:
    return "F27";
  case NCKEY_F28:
    return "F28";
  case NCKEY_F29:
    return "F29";
  case NCKEY_F30:
    return "F30";
  case NCKEY_F31:
    return "F31";
  case NCKEY_F32:
    return "F32";
  case NCKEY_F33:
    return "F33";
  case NCKEY_F34:
    return "F34";
  case NCKEY_F35:
    return "F35";
  case NCKEY_F36:
    return "F36";
  case NCKEY_F37:
    return "F37";
  case NCKEY_F38:
    return "F38";
  case NCKEY_F39:
    return "F39";
  case NCKEY_F40:
    return "F40";
  case NCKEY_F41:
    return "F41";
  case NCKEY_F42:
    return "F42";
  case NCKEY_F43:
    return "F43";
  case NCKEY_F44:
    return "F44";
  case NCKEY_F45:
    return "F45";
  case NCKEY_F46:
    return "F46";
  case NCKEY_F47:
    return "F47";
  case NCKEY_F48:
    return "F48";
  case NCKEY_F49:
    return "F49";
  case NCKEY_F50:
    return "F50";
  case NCKEY_F51:
    return "F51";
  case NCKEY_F52:
    return "F52";
  case NCKEY_F53:
    return "F53";
  case NCKEY_F54:
    return "F54";
  case NCKEY_F55:
    return "F55";
  case NCKEY_F56:
    return "F56";
  case NCKEY_F57:
    return "F57";
  case NCKEY_F58:
    return "F58";
  case NCKEY_F59:
    return "F59";
  case NCKEY_BACKSPACE:
    return "backspace";
  case NCKEY_CENTER:
    return "center";
  case NCKEY_ENTER:
    return "enter";
  case NCKEY_CLS:
    return "clear";
  case NCKEY_DLEFT:
    return "down+left";
  case NCKEY_DRIGHT:
    return "down+right";
  case NCKEY_ULEFT:
    return "up+left";
  case NCKEY_URIGHT:
    return "up+right";
  case NCKEY_BEGIN:
    return "begin";
  case NCKEY_CANCEL:
    return "cancel";
  case NCKEY_CLOSE:
    return "close";
  case NCKEY_COMMAND:
    return "command";
  case NCKEY_COPY:
    return "copy";
  case NCKEY_EXIT:
    return "exit";
  case NCKEY_PRINT:
    return "print";
  case NCKEY_REFRESH:
    return "refresh";
  case NCKEY_SEPARATOR:
    return "separator";
  case NCKEY_CAPS_LOCK:
    return "caps lock";
  case NCKEY_SCROLL_LOCK:
    return "scroll lock";
  case NCKEY_NUM_LOCK:
    return "num lock";
  case NCKEY_PRINT_SCREEN:
    return "print screen";
  case NCKEY_PAUSE:
    return "pause";
  case NCKEY_MENU:
    return "menu";
  // media keys, similarly only available through kitty's protocol
  case NCKEY_MEDIA_PLAY:
    return "play";
  case NCKEY_MEDIA_PAUSE:
    return "pause";
  case NCKEY_MEDIA_PPAUSE:
    return "play-pause";
  case NCKEY_MEDIA_REV:
    return "reverse";
  case NCKEY_MEDIA_STOP:
    return "stop";
  case NCKEY_MEDIA_FF:
    return "fast-forward";
  case NCKEY_MEDIA_REWIND:
    return "rewind";
  case NCKEY_MEDIA_NEXT:
    return "next track";
  case NCKEY_MEDIA_PREV:
    return "previous track";
  case NCKEY_MEDIA_RECORD:
    return "record";
  case NCKEY_MEDIA_LVOL:
    return "lower volume";
  case NCKEY_MEDIA_RVOL:
    return "raise volume";
  case NCKEY_MEDIA_MUTE:
    return "mute";
  case NCKEY_LSHIFT:
    return "left shift";
  case NCKEY_LCTRL:
    return "left ctrl";
  case NCKEY_LALT:
    return "left alt";
  case NCKEY_LSUPER:
    return "left super";
  case NCKEY_LHYPER:
    return "left hyper";
  case NCKEY_LMETA:
    return "left meta";
  case NCKEY_RSHIFT:
    return "right shift";
  case NCKEY_RCTRL:
    return "right ctrl";
  case NCKEY_RALT:
    return "right alt";
  case NCKEY_RSUPER:
    return "right super";
  case NCKEY_RHYPER:
    return "right hyper";
  case NCKEY_RMETA:
    return "right meta";
  case NCKEY_L3SHIFT:
    return "level 3 shift";
  case NCKEY_L5SHIFT:
    return "level 5 shift";
  case NCKEY_MOTION:
    return "mouse (no buttons pressed)";
  case NCKEY_BUTTON1:
    return "mouse (button 1)";
  case NCKEY_BUTTON2:
    return "mouse (button 2)";
  case NCKEY_BUTTON3:
    return "mouse (button 3)";
  case NCKEY_BUTTON4:
    return "mouse (button 4)";
  case NCKEY_BUTTON5:
    return "mouse (button 5)";
  case NCKEY_BUTTON6:
    return "mouse (button 6)";
  case NCKEY_BUTTON7:
    return "mouse (button 7)";
  case NCKEY_BUTTON8:
    return "mouse (button 8)";
  case NCKEY_BUTTON9:
    return "mouse (button 9)";
  case NCKEY_BUTTON10:
    return "mouse (button 10)";
  case NCKEY_BUTTON11:
    return "mouse (button 11)";
  default:
    return "unknown";
  }
}

}; // namespace Tools::NCPP

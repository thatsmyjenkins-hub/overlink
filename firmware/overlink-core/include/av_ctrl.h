#pragma once

#include <Arduino.h>

// Basement AV: Vizio SmartCast + Fire ADB + Sony AVR (+ CyberDeck IR fallback).
// Secrets: include/av_secrets.h (see av_secrets.example.h).

void avCtrlBegin();

// Scene hooks (CTRL volumes / inputs from cyd-basement-control)
bool avSceneApply(const String &sceneId, String &detail);

// Now-panel actions
bool avEnsureWatching(String &detail);          // Vizio on + Fire HDMI
bool avEnsurePs5(String &detail);               // Vizio on + PS5 HDMI (+WoL)
bool avLaunchApp(const char *appId, String &detail);  // nflx|yt|disney|prime
bool avVolDelta(int delta, int &levelOut, String &detail);
bool avSetMute(bool mute, String &detail);
bool avFireKey(int keycode, String &detail);    // Android keyevent

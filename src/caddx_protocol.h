#pragma once
#include <Arduino.h>

// Caddx Orca — Wi-Fi / HTTP CGI protocol (Hi3510 action-camera family).
//
// Provenance: reverse-engineered from the official "CaddxFPV" Android app
// (package com.gku.caddxfpv, decompiled with jadx). Full notes and copies of
// the relevant decompiled sources are under re/caddx_orca_protocol/ in this
// repo (gitignored, not part of the firmware).
//
// Unlike DJI Action / GoPro, Orca is NOT a BLE device for control purposes.
// The camera creates its own Wi-Fi access point (or the ESP32 could in
// principle join it after manual provisioning); the app joins that AP and
// issues plain HTTP GET requests to a CGI endpoint on the camera. There is
// no auth, no encryption, no session negotiation observed anywhere in the
// decompiled app for this camera family — every request is a bare
// unauthenticated GET.
//
// Response format for "get" endpoints is NOT JSON. It is a sequence of
// pseudo-JS variable assignments, verified byte-for-byte from
// StringParser.java / HttpRequest.java in the decompiled app:
//   var key="value";\r\n
//   var key2="value2";\r\n
//   ...
//
// "set"/action endpoints return HTTP 200 with no body on success
// (HttpProxy.doForSuccess: statusCode==200 is the *only* success check),
// except the record/photo action endpoints specifically, which return a
// body containing `SvrFuncResult="<code>"` on failure (Command.executeCommand).

#define CADDX_CGI_PATH "/cgi-bin/hi3510"

// ─── Endpoints (relative to CADDX_CGI_PATH) ──────────────────────────────────
#define CADDX_CGI_GET_DEVICE_ATTR   "/getdeviceattr.cgi"
#define CADDX_CGI_GET_ALL_INFO      "/getallinfo.cgi??"     // legacy (Hi3510 Common/Setting) — unconfirmed against real hardware
#define CADDX_CGI_GET_CUR_ALL_INFO  "/getcurallinfo.cgi"    // SigmaStar ("SS") family — hardware-confirmed on a real NewAPP Orca
#define CADDX_CGI_GET_BATTERY       "/getbatterycapacity.cgi?"
#define CADDX_CGI_GET_SD_STATE      "/getsdstate.cgi?"
// Hardware-confirmed exact query string, from SSCommandUtil.startRecord()/
// stopRecord(): UNIVERSAL_PART("record") + "?-cmd=start" — no leading "&".
#define CADDX_CGI_RECORD_START      "/record.cgi?-cmd=start"
#define CADDX_CGI_RECORD_STOP       "/record.cgi?-cmd=stop"
#define CADDX_CGI_RECORD2_START     "/record2.cgi?&-type=common&-cmd=start"
#define CADDX_CGI_RECORD2_STOP      "/record2.cgi?&-type=common&-cmd=stop"
#define CADDX_CGI_SET_WORKMODE      "/setcurworkmode.cgi?-workmode="

// getdeviceattr.cgi "type" values that select the record.cgi vs record2.cgi
// branch (Common.SENSOR_117 / Common.SENSOR_34220 in the decompiled app).
#define CADDX_SENSOR_TYPE_34220   "34220"

// getdeviceattr.cgi "hardversion" value that marks a "NewAPP"-generation
// device (DV.java / MainActivity.java: NewAPP devices always use record.cgi
// regardless of sensor type — see DV.executeCommand()).
#define CADDX_HARDVERSION_NEWAPP  "NewAPP"

// ─── getcurallinfo.cgi "state"/"mode" — SigmaStar ("SS") family ─────────────
// Hardware-confirmed on a real NewAPP Orca: getcurallinfo.cgi returned
// state="21" while idle. Source: SSystemWorkState.SS_STATE_WORKING/STANDBY.
// Unlike the legacy scheme below, "mode" here is NOT numeric — it's a free-
// form name string, one of the CADDX_MODE_NEW_* values already defined
// above (SSExchangeWorkMode.SS_*). No thermal/overheat event was found
// anywhere in the SigmaStar app package — this family doesn't appear to
// expose that over this API, so temp_over is left unset for NewAPP devices
// rather than guessed.
#define CADDX_SS_STATE_WORKING  20   // actively recording/capturing
#define CADDX_SS_STATE_STANDBY  21   // idle

// ─── getallinfo.cgi "state" values (Common.WORK_STATE_*) ────────────────────
// Legacy (non-NewAPP) Hi3510 devices only — transcribed from decompiled code
// but never exercised against real hardware (the one real Orca tested is a
// NewAPP/SigmaStar device and uses the scheme above instead).
#define CADDX_WORK_STATE_RECORD          0
#define CADDX_WORK_STATE_TIMELAPSE_PHOTO 1
#define CADDX_WORK_STATE_TIMER_PHOTO     2
#define CADDX_WORK_STATE_IDLE            3
#define CADDX_WORK_STATE_VIDEO_LOOP      4
#define CADDX_WORK_STATE_VIDEO_TIMELAPSE 5
#define CADDX_WORK_STATE_VIDEO_BURST     6

// ─── getallinfo.cgi "event" values (Common.EVENT_*) ──────────────────────────
// Legacy-only, same caveat as CADDX_WORK_STATE_* above.
#define CADDX_EVENT_NORMAL                    0
#define CADDX_EVENT_CHIP_TEMPERATURE_HIGH     7
#define CADDX_EVENT_BATTERY_TEMPERATURE_HIGH  8
#define CADDX_EVENT_CHIP_TEMPERATURE_ALARM    13
#define CADDX_EVENT_BATTERY_TEMPERATURE_ALARM 14

// ─── getallinfo.cgi "mode" values (Common.WORK_MODE_*) ───────────────────────
// Legacy-only, same caveat as CADDX_WORK_STATE_* above.
#define CADDX_WORK_MODE_PHOTO_SINGLE      0
#define CADDX_WORK_MODE_PHOTO_TIMER       1
#define CADDX_WORK_MODE_PHOTO_RAW         2
#define CADDX_WORK_MODE_MULTI_BURST       10
#define CADDX_WORK_MODE_MULTI_TIMELAPSE   11
#define CADDX_WORK_MODE_MULTI_CONTINUOUS  12
#define CADDX_WORK_MODE_VIDEO_NORMAL      20
#define CADDX_WORK_MODE_VIDEO_LOOP        21
#define CADDX_WORK_MODE_VIDEO_TIMELAPSE   22
#define CADDX_WORK_MODE_VIDEO_PHOTO       23
#define CADDX_WORK_MODE_VIDEO_SLOW        24
#define CADDX_WORK_MODE_VIDEO_QUICK       25
#define CADDX_WORK_MODE_VIDEO_LAPSE_BURST 26

// ─── setcurworkmode.cgi work-mode name strings ───────────────────────────────
// Two generations of firmware use two different naming conventions for the
// same CGI parameter (Common.java / CameraParmeras.java for legacy, vs.
// SSExchangeWorkMode.java for "NewAPP"/SigmaStar-style devices). Orca is
// hardware-confirmed NewAPP (see findings.md), so the CADDX_MODE_NEW_* set
// below is the one that applies — only submitting a value has not itself
// been tested yet. SSExchangeWorkMode.java also defines several names this
// driver never maps to (Burst Photo, Car Looping, Lapse Photo, Long
// Exposure, Quick Stories, Quick Video, Raw Photo, Timing Photo, Under
// Water, Video and Photo) since they don't have a DJI_MODE_* equivalent.
#define CADDX_MODE_LEGACY_NORMAL_VIDEO  "NormalVideo"
#define CADDX_MODE_LEGACY_NORMAL_PHOTO  "NormalPhoto"
#define CADDX_MODE_LEGACY_SLOW_REC      "SlowRec"
#define CADDX_MODE_LEGACY_VIDEO_LAPSE   "VideoLapse"

#define CADDX_MODE_NEW_NORMAL_VIDEO     "Normal Video"
#define CADDX_MODE_NEW_NORMAL_PHOTO     "Normal Photo"
#define CADDX_MODE_NEW_SLOW_MOTION      "Slow Motion"
#define CADDX_MODE_NEW_TIMELAPSE_VIDEO  "Timelapse Video"

// ─── "var key=\"value\";\r\n" response parser ────────────────────────────────
// Verified against StringParser.getKeyValueMap / HttpRequest.getMap: scans
// for "var ", then "=\"", then "\";" (CRLF not required for us to accept it —
// some responses may arrive without exact \r\n depending on webserver quirks,
// so we terminate on the closing quote rather than requiring the full tail).
inline bool caddx_parse_var(const String &body, const char *key, String &out) {
    const String needle = String("var ") + key + "=\"";
    int start = body.indexOf(needle);
    if (start < 0) return false;
    start += needle.length();
    int end = body.indexOf('"', start);
    if (end < 0) return false;
    out = body.substring(start, end);
    return true;
}

inline bool caddx_parse_var_int(const String &body, const char *key, long &out) {
    String s;
    if (!caddx_parse_var(body, key, s)) return false;
    out = s.toInt();
    return true;
}

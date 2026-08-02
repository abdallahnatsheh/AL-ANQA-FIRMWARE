---
title: العربية
permalink: /ar/
nav_exclude: true
lang: ar
description: العنقاء — برنامج أمن هجومي لجهاز LilyGo T-Deck
---

<p align="center">
  <img src="{{ site.baseurl }}/assets/images/banner.png" width="480"/>
</p>

<p align="center">
  <a href="{{ site.baseurl }}/" class="btn btn-primary">🌐 English</a>
</p>

<div dir="rtl" markdown="1" style="text-align:right">

# العنقاء (Al-Anqa)

**برنامج (Firmware) للأمن الهجومي لجهاز LilyGo T-Deck — واجهة أوامر للهاكر في جيبك.**

يحوّل العنقاء جهاز LilyGo T-Deck إلى طرفية اختبار اختراق محمولة في الجيب. لا قوائم ولا واجهة رسومية — فقط مؤشّر يومض، ولوحة مفاتيح فعلية، وحزمة كاملة من أدوات الأمن الهجومي تعمل على معالج ESP32-S3.

---

> ⚠️ **إخلاء مسؤولية قانوني** — للاستخدام في اختبارات الأمن المصرّح بها، ومسابقات CTF، والأغراض التعليمية فقط. احصل دائمًا على إذن كتابي قبل أي اختبار.

---

## التوثيق

### 🚀 ابدأ من هنا

- [**دليل البدء**]({{ site.baseurl }}/getting-started) — التنزيل (Flash)، الإقلاع الأول، إعداد بطاقة SD، أول الأوامر
- [**مرجع لوحة المفاتيح**]({{ site.baseurl }}/keyboard) — مفتاح Sym، الإكمال التلقائي، السجل، تحرير المؤشّر، كرة التتبّع (Trackball)
- [**أمثلة عملية (Workflows)**]({{ site.baseurl }}/workflows) — أمثلة متكاملة: التقاط WPA2، استطلاع الشبكة، نظام كشف التسلّل (IDS)، كشف أجهزة التتبّع
- [**T-Deck مقابل T-Deck Plus**]({{ site.baseurl }}/hardware) — الفارق الوحيد في العتاد هو وجود GPS
- [**حل المشكلات**]({{ site.baseurl }}/troubleshooting) — فشل الرفع، مشاكل SD، WiFi، BLE، GPS، شاشة القفل

---

### 📡 WiFi

- [المسح والاتصال]({{ site.baseurl }}/wifi-scan) — `scanwifi` · `connectwifi`
- [مراقب WiFi]({{ site.baseurl }}/wifimon) — `wifimon`
- [Wardrive]({{ site.baseurl }}/wardrive) — `wardrive` — دمج WiFi مع GPS ثم تصدير إلى WiGLE CSV (لجهاز Plus فقط)
- [بيانات اعتماد WiFi]({{ site.baseurl }}/wifi-credentials) — `wifipass` · `wp export` · `wp clear`
- [مُغيّر MAC]({{ site.baseurl }}/macchanger) — `macchanger`
- [هجمات WiFi]({{ site.baseurl }}/wifi-attacks)
  - [إلغاء المصادقة (Deauth)]({{ site.baseurl }}/deauth) — `deauth`
  - [التوأم الشرير (Evil Twin)]({{ site.baseurl }}/eviltwin) — `eviltwin`
  - [الشبكات المخفية (Hidden SSID)]({{ site.baseurl }}/hiddenssid) — `hiddenssid`
  - [التقاط WPA (WPA Sniff)]({{ site.baseurl }}/wpasniff) — `wpasniff`
  - [هجوم PMKID]({{ site.baseurl }}/pmkid) — `pmkid`
  - [نظام الحماية WGuard]({{ site.baseurl }}/wguard) — `wguard`
  - [إغراق المنارات (Beacon Flood)]({{ site.baseurl }}/beacon-flood) — `beaconflood`

---

### 🌐 الشبكة (Network)

- [اكتشاف الأجهزة (Net Discover)]({{ site.baseurl }}/netdiscover) — `netdiscover`
- [التجسّس على الشبكة (Net Spy)]({{ site.baseurl }}/netspy) — `netspy` / `ns` — **[تجريبي]** استطلاع سلبي للأجهزة رغم عزل العملاء (AirSnitch)
- [فحص العزل (Iso Scan)]({{ site.baseurl }}/isoscan) — `isoscan` / `is` — **[تجريبي]** تدقيق فعّال للعزل: حقن GTK والتقاط (AirSnitch)
- [فحص المنافذ (Port Scan)]({{ site.baseurl }}/portscan) — `portscan` · `ps top` · ملتقط البانر · بصمة نظام التشغيل
- [Ping]({{ site.baseurl }}/ping) — `ping`
- [عميل SSH]({{ site.baseurl }}/ssh) — `ssh` — طرفية ملوّنة تفاعلية مع تمرير للخلف

---

### 🔵 Bluetooth

- [مسح BLE]({{ site.baseurl }}/scanblue) — `scanblue`
- [معلومات BLE]({{ site.baseurl }}/bleinfo) — `bleinfo`
- [كشف التتبّع]({{ site.baseurl }}/trackme) — `trackme`
- [Fast Pair]({{ site.baseurl }}/fastpair) — `fastpair`
- [إغراق BLE (BLE Spam)]({{ site.baseurl }}/blespam) — `blespam`
- [الرفيق (Buddy)]({{ site.baseurl }}/buddy) — `buddy`
- [لوحة مفاتيح Bluetooth]({{ site.baseurl }}/btkbd) — `btkbd`

---

### 🔌 USB

- [التخزين الكتلي (Mass Storage)]({{ site.baseurl }}/usbmsc) — `usbmsc`
- [لوحة مفاتيح USB]({{ site.baseurl }}/usbkbd) — `usbkbd`
- [BadUSB]({{ site.baseurl }}/usbexec) — `usbexec`
- [محرّك الفأرة (Mouse Jiggler)]({{ site.baseurl }}/jiggle) — `jiggle`

---

### ⚙️ النظام (System)

- [المساعدة والدليل]({{ site.baseurl }}/help-man) — `help` · `man` · `show` · `clear` · `MATRIX`
- [معلومات الجهاز]({{ site.baseurl }}/info) — `info`
- [توفير الطاقة]({{ site.baseurl }}/pwrsave) — `pwrsave` · `sleep`
- [شاشة القفل]({{ site.baseurl }}/lock) — `lock`
- [الوضع المتخفّي (Undercover)]({{ site.baseurl }}/system#notes--nt-and-undercover--uc--undercover-mode) — `notes` · `undercover`
- [المنطقة الزمنية]({{ site.baseurl }}/tz) — `tz`
- [الصوت والإشعارات]({{ site.baseurl }}/audio) — `volume` · `notif` · `test spk`
- [أوامر SD]({{ site.baseurl }}/sd-commands) — `sdinfo` · `sdls` · `cd` · `cat` · `edit` · `rm` · `sdformat`
- [التشخيص]({{ site.baseurl }}/diagnostics) — `gps on/off/test` · `test spk` · `test mic` · `test lora`
- [تخطيط بطاقة SD]({{ site.baseurl }}/sdcard) — مرجع تنظيم الملفات
- [شاشة الإقلاع المخصّصة]({{ site.baseurl }}/splash) — استبدل صورة الإقلاع بصورة PNG خاصة بك

---

## البدء السريع

**المتطلبات:** [VSCode](https://code.visualstudio.com) مع إضافة [PlatformIO](https://platformio.org)

```bash
git clone https://github.com/abdallahnatsheh/AL-ANQA-FIRMWARE
# افتح المشروع في VSCode ← اختر env:T-Deck أو env:T-Deck-Plus ← اضغط Upload
```

> **لا يمكنك الرفع؟** اضغط مطوّلًا على زر كرة التتبّع، ثم وصّل كابل USB، ثم أعد المحاولة — هذا يجبر الجهاز على الدخول في وضع التنزيل (Download Mode).

---

## العتاد (Hardware)

| المكوّن | التفاصيل |
|-----------|---------|
| الأجهزة | LilyGo T-Deck · LilyGo T-Deck Plus |
| المعالج (MCU) | ESP32-S3 (ذاكرة Flash سعة 16 MB، وذاكرة PSRAM سعة 8 MB) |
| الشاشة | 320×240 ST7789 TFT |
| الإدخال | لوحة مفاتيح QWERTY فعلية + كرة تتبّع |
| الراديو | WiFi 2.4 GHz · Bluetooth 5 · LoRa SX1262 |
| GPS | L76K / u-blox M10Q (لجهاز T-Deck Plus فقط) |

</div>

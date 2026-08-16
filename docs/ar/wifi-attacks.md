---
title: هجمات WiFi
permalink: /wifi-attacks/
lang: ar
parent: WiFi
nav_order: 5
has_children: true
---

<div dir="rtl" markdown="1" style="text-align:right">

# هجمات WiFi

> تتطلّب جميع الهجمات أن تكون ضمن نطاق الهدف. شغّل `scanwifi` (`sw`) أوّلًا لتعبئة فهرس الشبكات.

| الدليل | الأمر | ما يفعله |
|-------|---------|-------------|
| [إلغاء المصادقة]({{ site.baseurl }}/deauth) | `deauth` / `da` | فصل العملاء عن نقطة الوصول |
| [التوأم الشرير]({{ site.baseurl }}/eviltwin) | `eviltwin` / `et` | نقطة وصول مزيّفة + بوّابة أَسر (Captive Portal) |
| [الشبكات المخفية]({{ site.baseurl }}/hiddenssid) | `hiddenssid` / `hs` | كشف أسماء الشبكات المخفية |
| [التقاط WPA]({{ site.baseurl }}/wpasniff) | `wpasniff` / `ws` | التقاط مصافحة WPA2 وكسرها (يتطلّب عميلًا) |
| [هجوم PMKID]({{ site.baseurl }}/pmkid) | `pmkid` / `pm` | التقاط PMKID وكسره — **نشِط بلا عميل افتراضيًّا** (`pm passive` = مسح صامت)، لا يتطلّب عميلًا |
| [Karma]({{ site.baseurl }}/karma) | `karma` / `km` | حزمة نقطة الوصول المزيّفة — حصاد الطلبات، بصمة PNL، نصف مصافحة / إغراء ببوّابة |
| [كاسر الحزم]({{ site.baseurl }}/capcrack) | `crack` / `cc` | كسر ملف `.cap` دون اتصال (مصافحة أو PMKID) بقوائم كلمات |
| [نظام الحماية WGuard]({{ site.baseurl }}/wguard) | `wguard` / `wg` | كشف تسلّل WiFi سلبي |
| [إغراق المنارات]({{ site.baseurl }}/beacon-flood) | `beaconflood` / `bf` | إغراق قوائم مسح WiFi بأسماء مزيّفة |
| [WPS]({{ site.baseurl }}/wps) | `wps` | استطلاع WPS + مولّد PIN + اتصال بالزرّ |
| [حيوان Pwnagotchi]({{ site.baseurl }}/pwn) | `pwn` / `pw` | ذاتي — **تجوّل AI تكيّفي** (كل الـ13 قناة؛ `basic`=1/6/11) + التقاط مصافحات/PMKID (استدرار بلا عميل) + **كسر على الجهاز** (نشط/خفي/سلبي) |

---

لكلّ هجوم دليله الخاص — اختر واحدًا من الجدول أعلاه أو من الشريط الجانبي.

</div>

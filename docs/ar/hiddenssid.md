---
title: الشبكات المخفية
permalink: /hiddenssid/
lang: ar
parent: هجمات WiFi
grand_parent: WiFi
nav_order: 3
---

<div dir="rtl" markdown="1" style="text-align:right">

# الشبكات المخفية (Hidden SSID)

## `hiddenssid` / `hs`

يكشف اسم SSID لشبكة مخفية عبر إجبار العملاء على إعادة الارتباط.

```
CMD> hs <index|bssid> [channel] [silent]
CMD> hs 4
CMD> hs AA:BB:CC:DD:EE:FF 11
CMD> hs 4 11 silent
```

### كيف يعمل

1. يرسل دفعات إلغاء مصادقة كل 3 ثوانٍ لفصل العملاء عن نقطة الوصول
2. يترصّد استجابات الفحص (النوع الفرعي 5) وطلبات الارتباط (النوع الفرعي 0) المطابقة للـ BSSID الهدف
3. يستخرج اسم SSID من الإطار الملتقَط

### عند العثور

- يصدر نغمة مزدوجة (ما لم يُحدَّد `silent`)
- يعرض اسم SSID على الشاشة
- يحفظ `BSSID,SSID,ch` في `/apps/hiddenssid/found.csv`
- يظهر الاسم بصيغة `~name` باللون السماوي في مسح `sw` التالي

اضغط `q` للإيقاف.

</div>

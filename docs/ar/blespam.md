---
title: إغراق BLE
permalink: /blespam/
lang: ar
parent: Bluetooth
nav_order: 5
---

<div dir="rtl" markdown="1" style="text-align:right">

# إغراق BLE

## `blespam` / `bs` — إغراق إعلانات BLE

يغمر الهواء بإعلانات BLE تُطلِق نوافذ اقتران أو إشعارات على الأجهزة القريبة. ويُعشوَئ عنوان MAC كل دورة لتجاوز مرشّحات الأجهزة.

```
CMD> bs              # التنقّل بين كل المصنّعين
CMD> bs apple        # نوافذ iOS Continuity
CMD> bs android      # Google Fast Pair (أندرويد)
CMD> bs ms           # إشعار Windows Swift Pair
CMD> bs samsung      # نافذة ملحق Samsung Galaxy
CMD> bs all          # التنقّل بين المصنّعين الأربعة
```

| المصنّع | ما يظهر على الهدف |
|--------|----------------------|
| `apple` | نافذة اقتران AirPods / Apple TV على iOS |
| `android` | "سمّاعات قريبة" (Google Fast Pair) |
| `ms` | إشعار Windows Swift Pair |
| `samsung` | نافذة اقتران ملحق Samsung Galaxy |

| المفتاح | الإجراء |
|-----|--------|
| `l` / `a` | المصنّع التالي / السابق |
| `q` | الإيقاف |

</div>

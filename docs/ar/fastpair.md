---
title: Fast Pair
permalink: /fastpair/
lang: ar
parent: Bluetooth
nav_order: 4
---

<div dir="rtl" markdown="1" style="text-align:right">

# Fast Pair

## `fastpair` / `fp` — حزمة هجوم Google Fast Pair

```
CMD> fp              # قائمة تفاعلية
CMD> fp scan         # مسح BLE لأجهزة Fast Pair
CMD> fp spam         # إغراق نوافذ اقتران أندرويد
CMD> fp h <index>    # اختطاف GATT لجهاز محدّد
CMD> fp h all        # اختطاف كل الأجهزة الممسوحة
```

### `fp scan`

يمسح أجهزة BLE التي تُعلن معرّف خدمة Fast Pair (`0xFE2C`) لمدّة 5 ثوانٍ. يعرض الفهرس، ومعرّف الطراز، والاسم، و MAC، و RSSI.

| المفتاح | الإجراء |
|-----|--------|
| `h <n>` | اختطاف GATT للجهاز رقم n |
| `s` | التبديل لوضع الإغراق |
| `q` | الخروج / إلغاء المسح مبكّرًا |

### `fp spam`

يغمر الهواء بحزم إعلان Fast Pair. تستخدم كل دورة عنوان MAC عشوائيًّا جديدًا وتُعلن 10 ثوانٍ. فتُظهر أجهزة أندرويد القريبة نوافذ اقتران Google Fast Pair.

اضغط `q` للإيقاف.

### `fp h <index>` — WhisperPair

يتّصل بالجهاز الهدف عبر GATT ويقرأ مفتاحه العامّ المضادّ للانتحال. ويُخزَّن المفتاح في `/apps/fastpair/keys.csv` ويُسجَّل في `/apps/fastpair/sniff.csv`.

> ضع الجهاز الهدف في وضع الاقتران لأفضل النتائج — فالأجهزة في وضع الاقتران تكشف المفتاح المضادّ للانتحال مباشرةً.

</div>

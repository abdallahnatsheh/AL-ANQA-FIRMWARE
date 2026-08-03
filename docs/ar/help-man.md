---
title: المساعدة والدليل
permalink: /help-man/
lang: ar
parent: System
nav_order: 1
---

<div dir="rtl" markdown="1" style="text-align:right">

# المساعدة والدليل

## `help` / `hlp` — قائمة الأوامر

```
CMD> help            # كل الأوامر مصنّفة حسب الفئة
CMD> help deauth     # تفاصيل أمر واحد
CMD> hlp da
```

تُصنَّف الأوامر حسب الفئة (System و WiFi و Network و Bluetooth و SD Card و Diagnostics)، وتُقسَّم 5 لكل صفحة.

| المفتاح | الإجراء |
|-----|--------|
| `l` / `a` | الصفحة التالية / السابقة |
| `q` | الخروج |

---

## `man` / `mn` — صفحات الدليل

```
CMD> man deauth
CMD> mn da
```

دليل كامل على الجهاز لأي أمر — الصياغة، الخطوات، المفاتيح، الخيارات، الملفّات، التحذيرات. والأسماء المختصرة تعمل (`mn da` = `mn deauth`).

| المفتاح | الإجراء |
|-----|--------|
| `l` / `a` | الصفحة التالية / السابقة |
| `q` | الخروج |

---

## `show` / `sh` — إعادة عرض آخر مسح

```
CMD> show wifi      # آخر نتيجة scanwifi
CMD> show ble       # آخر نتيجة scanblue
CMD> show hosts     # آخر نتيجة netdiscover
```

يعيد عرض جدول آخر مسح مخزَّن دون تشغيل مسح جديد. ويعرض `No scan data` إن لم يُشغَّل ذلك المسح بعد في هذه الجلسة.

---

## `clear` / `clr` — مسح الشاشة

```
CMD> clear
```

يمسح منطقة الخرج ويعيد ضبط المِحثّ.

---

## `MATRIX` / `matrix` — رسوم Matrix

```
CMD> MATRIX
```

يشغّل رسوم مطر Matrix الرقمي. اضغط `q` للخروج.

</div>

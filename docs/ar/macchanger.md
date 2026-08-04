---
title: مُغيّر MAC
permalink: /macchanger/
lang: ar
parent: WiFi
nav_order: 4
---

<div dir="rtl" markdown="1" style="text-align:right">

# مُغيّر MAC

## `macchanger` / `mc` — انتحال عنوان MAC

ينتحل عنوان MAC الخاص بواجهة STA على مستوى المُشغّل باستخدام عنوان أُحادي البثّ مُدار محلّيًّا (locally-administered) — البِت 1 من البايت الأوّل مضبوط، والبِت 0 صفر.

```
CMD> mc on                          # تفعيل الانتحال
CMD> mc off                         # التعطيل واستعادة عنوان MAC الحقيقي للعتاد
CMD> mc random                      # تطبيق عنوان MAC عشوائي جديد فورًا
CMD> mc set AA:BB:CC:DD:EE:FF       # تطبيق عنوان MAC محدّد
CMD> mc restore on|off              # تفعيل/تعطيل الاستعادة التلقائية عند الإقلاع
CMD> mc target wifi|bt|both         # تحديد الواجهة التي يُنتحَل عنوانها
CMD> mc status                      # عرض العنوان الحالي والحالة
```

تُحفَظ الإعدادات في `/config/macchanger.conf` على بطاقة SD وتُستعاد عند الإقلاع.

### متى يُطبَّق

يُطبَّق انتحال عنوان MAC تلقائيًّا في هذين الأمرين فقط:

| الأمر | متى |
|---------|------|
| `scanwifi` / `sw` | قبل بدء المسح |
| `connectwifi` / `cw` | قبل الاتصال |

ولا يُطبَّق أثناء `wifimon` أو `deauth` أو `wpasniff` أو أي أداة هجوم — فالأُطُر المحقونة تستخدم عناوين مصدر منتحَلة خاصة بها، والمُلتقِطات السلبية لا تُرسِل شيئًا.

</div>

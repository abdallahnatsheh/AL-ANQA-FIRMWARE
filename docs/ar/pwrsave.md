---
title: توفير الطاقة
permalink: /pwrsave/
lang: ar
parent: System
nav_order: 3
---

<div dir="rtl" markdown="1" style="text-align:right">

# توفير الطاقة

## `pwrsave` / `psv`

نظام خمول من مستويين يُعتِم الشاشة ثم يُطفئها.

```
CMD> psv status
CMD> psv on / psv off
CMD> psv set dim <seconds>           # مهلة الإعتام عند الخمول (الافتراضي: 120 ثانية)
CMD> psv set screenoff <seconds>     # مهلة إطفاء الشاشة (الافتراضي: 300 ثانية)
CMD> psv set screenoffmode on|off
```

| المستوى | الافتراضي | السلوك |
|------|---------|-----------|
| الإعتام | دقيقتان | يخفّض السطوع |
| إطفاء الشاشة | 5 دقائق | السطوع = 0، وأي مفتاح يستعيدها |

**الإعتام حسب البطارية** — يُعتِم تلقائيًّا حين تنخفض البطارية دون العتبة، بصرف النظر عن مؤقّت الخمول.

تُحفَظ الإعدادات في `/config/pwrsave.conf` على بطاقة SD وتُستعاد عند الإقلاع.

</div>

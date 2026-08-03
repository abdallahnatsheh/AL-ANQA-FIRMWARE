---
title: Bluetooth
permalink: /bluetooth/
lang: ar
nav_order: 8
has_children: true
---

<div dir="rtl" markdown="1" style="text-align:right">

# Bluetooth

مسح BLE، والمراقبة، وكشف أجهزة التتبّع، واستخدام T-Deck لوحةَ مفاتيح Bluetooth. لكل أداة صفحتها (على اليسار) بكامل التفاصيل.

> يتقاسم BLE و WiFi هوائيًّا واحدًا — أوقِف أي مسح/هجوم WiFi نشط قبل تشغيل أداة Bluetooth.

---

## المسح والمراقبة

| الأداة | الأمر | ما تفعله |
|------|---------|--------------|
| [مسح BLE]({{ site.baseurl }}/scanblue) | `sbl` | مسح فعّال لأجهزة BLE القريبة |
| [مراقب إعلانات BLE]({{ site.baseurl }}/bmon) | `bmon` | مترصّد إعلانات **سلبي** — يفكّ iBeacon / Eddystone / الأسماء، ويسجّل إلى SD |
| [معلومات BLE]({{ site.baseurl }}/bleinfo) | `bi` | اتصال + تعداد GATT؛ قراءة/كتابة، تشويش، مطرقة قراءة، تدقيق أمني |

## الكشف والمراقبة

| الأداة | الأمر | ما تفعله |
|------|---------|--------------|
| [كشف التتبّع]({{ site.baseurl }}/trackme) | `tm` | كشف أجهزة تتبّع بأسلوب AirTag/Tile تتبعك فعليًّا |
| [مراقبة MAC]({{ site.baseurl }}/macwatch) | `macwatch` | قائمة مراقبة عناوين MAC (WiFi + BLE) بتنبيهات قرب |

## الهجمات و HID

| الأداة | الأمر | ما تفعله |
|------|---------|--------------|
| [Fast Pair]({{ site.baseurl }}/fastpair) | `fp` | حزمة هجوم Google Fast Pair |
| [إغراق BLE]({{ site.baseurl }}/blespam) | `bs` | إغراق إعلانات BLE (نوافذ اقتران) |
| [الرفيق]({{ site.baseurl }}/buddy) | `bd` | ريموت/حيوان أليف BLE لسطح المكتب |
| [لوحة مفاتيح Bluetooth]({{ site.baseurl }}/btkbd) | `bk` | استخدام T-Deck لوحةَ مفاتيح وفأرة BLE |

---

## سير عمل نموذجي

```
CMD> sbl           # مسح الأجهزة، لاحظ فهرسًا
CMD> bi 3          # تعداد خدمات GATT للجهاز رقم 3
CMD> tm            # أو: مراقبة أجهزة التتبّع التي تتبعك
```

</div>

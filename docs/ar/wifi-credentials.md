---
title: بيانات اعتماد WiFi
permalink: /wifi-credentials/
lang: ar
parent: WiFi
nav_order: 3
---

<div dir="rtl" markdown="1" style="text-align:right">

# إدارة بيانات اعتماد WiFi

يخزّن العنقاء بيانات اعتماد WiFi في مكانين: **NVS** (فلاش الجهاز) و**بطاقة SD** (`/wpa_supplicant.conf`). ويُستخدَم كلاهما تلقائيًّا — فلا داعي للتفكير في أيّهما.

---

## كيف تعمل بيانات الاعتماد

عند اتصالك بشبكة، يستخرج العنقاء كلمة المرور بهذا الترتيب:

1. **NVS** — الأسرع، يبقى بعد إعادة التشغيل، محلّي للجهاز
2. **بطاقة SD** (`/wpa_supplicant.conf`) — مشترَك بين الأجهزة، ومتوافق مع Linux
3. **الطلب** — تكتب كلمة المرور؛ وتُحفَظ تلقائيًّا في NVS و SD معًا

وعند نجاح الاتصال تُلحَق الشبكة دائمًا بملفّ `/wpa_supplicant.conf` على بطاقة SD (إن وُجِدت) بصيغة `wpa_supplicant` القياسية في Linux.

> **إدخال `=` في كلمة المرور (T-Deck / T-Deck-Plus):** لوحة مفاتيح LilyGo الأصلية لا تحوي مفتاح `=`. يُعيد العنقاء تعيين **SYM + 0** إلى `=` ليتمكّن المستخدم من كتابة أي PSK يحتوي عليه عند موجّه `cw`. تفاصيل أكثر في [مرجع لوحة المفاتيح](keyboard.md).

---

## `wifipass` / `wp` — إدارة بيانات الاعتماد المحفوظة

```
CMD> wp            # عرض كلمات المرور المحفوظة
CMD> wp export     # نسخ شبكات NVS ← ملفّ SD wpa_supplicant.conf
CMD> wp clear      # مسح جميع بيانات الاعتماد المحفوظة
```

بلا وسيط، يعرض جميع بيانات الاعتماد في جدول مُقسَّم لصفحات. يقرأ من بطاقة SD أوّلًا، ويعود إلى NVS إن لم تكن هناك بطاقة أو ملفّ. أمّا الأمران الفرعيّان `export` و`clear` (اللذان كانا سابقًا `wifiexport` / `clearwifi`) فموثّقان أدناه.

يعرض الرأس المصدرَ النشط بالألوان: **SD** (أخضر) أو **NVS** (أصفر).

| العرض | المعنى |
|---------|---------|
| نصّ عادي | كلمة مرور صالحة للاستخدام |
| `[open]` | شبكة مفتوحة، لا حاجة لكلمة مرور |
| `[hex-psk]` | مُدخَل مُجزَّأ بأسلوب Linux — لا يمكن للعنقاء استخدام التجزئة؛ شغّل `cw <ssid>` لإدخال كلمة المرور مرّة واحدة وإصلاحه |
| `~name` بالسماوي | شبكة مخفية (`scan_ssid=1`) |

| المفتاح | الإجراء |
|-----|--------|
| `l` / `a` | الصفحة التالية / السابقة |
| `q` | الخروج |

---

## `wp export` — تصدير NVS إلى SD

```
CMD> wp export
```

يصدّر جميع شبكات WiFi المخزّنة في NVS إلى `/wpa_supplicant.conf` على بطاقة SD. مفيد بعد الاتصال بشبكات دون إدخال بطاقة — شغّل `wp export` بعد إدخال البطاقة لمزامنتها.

---

## `wp clear` — مسح بيانات الاعتماد المحفوظة

```
CMD> wp clear
```

يمسح جميع كلمات مرور WiFi المحفوظة من NVS. وعند الاتصال التالي بشبكة معروفة سيُطلَب إدخال كلمة المرور. ولا يقطع الجلسة النشطة.

---

## `connectwifi` / `cw` — الاتصال بالاسم

```
CMD> cw <index>       # استخدام الرقم من آخر مسح sw
CMD> cw <ssid>        # الاتصال حسب اسم SSID — بلا حاجة لمسح
```

يفيد الاتصال بالاسم عندما:
- تكون الشبكة مخفية (لا تبثّ اسمها)
- تكون الشبكة على بطاقة SD من جلسة سابقة أو مستورَدة من Linux
- لا ترغب في إجراء مسح كامل

يرسل العنقاء طلبات فحص موجّهة للشبكات المخفية تلقائيًّا — بلا إعداد إضافي.

---

## ملفّ بطاقة SD: ‏`/wpa_supplicant.conf`

يستخدم العنقاء الصيغة نفسها التي يستخدمها `wpa_supplicant` في Linux. وكل مُدخَل يكتبه يبدو هكذا:

```
network={
    ssid="MyNetwork"
    #psk="mypassword"
    psk="mypassword"
}
```

- `psk="plain"` — يتّصل Linux بها مباشرةً
- `#psk="plain"` — تعليق يبقى إن أعاد Linux التجزئة؛ يقرؤه العنقاء لاستعادة كلمة المرور النصّية
- `/wpa_supplicant.bak` — يُنشَأ تلقائيًّا أوّل مرّة يعدّل فيها العنقاء الملفّ؛ فنسختك الأصلية آمنة دائمًا

---

## المزامنة مع Linux

### Raspberry Pi / Linux بلا واجهة ← T-Deck

انسخ مباشرةً — الصيغة متطابقة:

```bash
sudo cp /etc/wpa_supplicant/wpa_supplicant.conf /media/$USER/<sdcard>/wpa_supplicant.conf
```

أدخِل بطاقة SD، وشغّل T-Deck. تصبح كل الشبكات متاحة فورًا.

---

### سطح مكتب Linux (NetworkManager) ← T-Deck

لا يستخدم NetworkManager الملفّ `/etc/wpa_supplicant/wpa_supplicant.conf` مخزنًا رئيسيًّا — بل يحفظ كلمات المرور نصّيًّا في `/etc/NetworkManager/system-connections/`. وهذه هجرة تُجرى مرّة واحدة:

**الخطوة 1 — اسرد كل أسماء الشبكات وكلمات مرورها:**

```bash
sudo nmcli -s -g NAME,802-11-wireless.ssid,802-11-wireless-security.psk connection show
```

المُخرَج:
```
HomeWiFi  :HomeWiFi  :mypassword123
WorkNet   :WorkNet   :workpass456
```

**الخطوة 2 — أنشئ ملفّ SD:**

```bash
wpa_passphrase "HomeWiFi" "mypassword123" >> /media/$USER/<sdcard>/wpa_supplicant.conf
wpa_passphrase "WorkNet"  "workpass456"  >> /media/$USER/<sdcard>/wpa_supplicant.conf
```

تمّ. هذا **إعداد لمرّة واحدة**. وبعده، أي شبكة جديدة تتّصل بها على T-Deck تُلحَق بالملفّ تلقائيًّا — انسخه إلى Linux ليعمل هناك أيضًا.

---

### ‏T-Deck ← Linux

انسخ ملفّ SD إلى أي جهاز Linux:

```bash
sudo cp /media/$USER/<sdcard>/wpa_supplicant.conf /etc/wpa_supplicant/wpa_supplicant.conf
sudo systemctl restart wpa_supplicant
```

يقبل Linux الصيغة `psk="plaintext"` ويتّصل فورًا. بلا أي تحويل.

---

## تنبيه ‏`update_config=1`

إن كان Linux مضبوطًا على `update_config=1`، فسيعيد كتابة `wpa_supplicant.conf` بعد الاتصال و**يحذف كل التعليقات** — بما فيها سطر `#psk=`. فيصبح المُدخَل:

```
network={
    ssid="MyNetwork"
    psk=a3f9bc12e4...    ← تجزئة من 64 حرفًا، لا يمكن للعنقاء استخدامها
}
```

فإن نسخت هذا الملفّ إلى T-Deck، ظهرت الشبكات المتأثّرة بوسم `[hex-psk]`.

**الإصلاح:** شغّل `cw <ssid>`، وأدخِل كلمة المرور مرّة واحدة. يتّصل العنقاء، ويحفظ كلمة المرور النصّية في NVS، ويرقّي مُدخَل SD. فتعمل الشبكة دائمًا بعدها دون إعادة إدخال.

---

## جدول توافق الصِّيَغ

| صيغة Linux | سلوك العنقاء |
|---|---|
| `psk="plaintext"` | ✅ يتّصل مباشرةً |
| `#psk="plain"` + `psk=hexhash` (مُخرَج `wpa_passphrase`) | ✅ يستعيد النصّ من التعليق |
| `psk=hexhash` فقط (حُذِف التعليق بـ `update_config=1`) | ⚠️ يظهر `[hex-psk]` — أدخِل كلمة المرور مرّة لإصلاحه |
| `key_mgmt=NONE` (شبكة مفتوحة) | ✅ يتّصل مباشرةً |
| `scan_ssid=1` (شبكة مخفية) | ✅ يرسل `cw <ssid>` فحصًا موجّهًا |
| `priority=` و`bssid=` و`proto=` و`pairwise=` | ✅ تُحلَّل أو تُتجاهَل بصمت، ولا يتلف الملفّ أبدًا |
| `ctrl_interface=` و`update_config=` و`country=` | ✅ تُتجاهَل بصمت |
| `ssid=4d79...` (SSID بترميز سُداسي، غير ASCII) | ❌ غير مدعوم — تُتخطّى الشبكة |
| `key_mgmt=WPA-EAP` (المؤسّسات / الشهادات) | ❌ غير مدعوم على ESP32 |

</div>

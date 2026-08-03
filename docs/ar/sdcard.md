---
title: تخطيط بطاقة SD
permalink: /sdcard/
lang: ar
parent: System
nav_order: 9
---

<div dir="rtl" markdown="1" style="text-align:right">

# تخطيط بطاقة SD

هيّئ البطاقة بنظام **FAT32**. وأدخِلها قبل التشغيل.

---

## قائمة البدء السريع

- [ ] بطاقة microSD مُهيّأة FAT32 (أي حجم)
- [ ] أدخِل البطاقة قبل التشغيل
- [ ] الإقلاع — تُنشَأ تلقائيًّا `/config/` و`/config/notification/` و`/apps/<tool>/` (واحد لكل أمر) و`/apps/README.txt`
- [ ] شغّل `sdinfo` لتأكيد اكتشاف البطاقة
- [ ] شغّل `sdls /` للتحقّق من بنية المجلّدات

تُنشَأ شجرة `/config` + `/apps/<tool>` كاملةً **بشكل استباقي** عند أوّل إقلاع/تهيئة عبر `ensureTreeStructure()` — فمجلّد كل أداة موجود من البداية. ويسرد ملفّ `/apps/README.txt` على الجهاز دائمًا خريطة المجلّد←الأمر الحالية — اقرأه بـ `cat /apps/README.txt` إن نسيت مكان شيء.

> **ملاحظة على التخطيط (v2):** يجمع تخطيط SD بيانات كل أداة — سجلّات، والتقاطات، وقوائم كلمات، وإعدادات خاصّة — تحت مجلّدها `/apps/<tool>/`. وتقيم الإعدادات العامّة (pwrsave و lockscreen و notif و clock و macchanger) في `/config/`. ولم يعد هناك مجلّد `/logs/`. وإن كنت تُرقّي من بناء أقدم، فالملفّات في المسارات القديمة تُترَك مكانها لكن لا تُقرَأ — انقلها يدويًّا للمواقع الجديدة أدناه إن أردت الاحتفاظ بها.

---

## المجلّدات — تُنشَأ تلقائيًّا عند أوّل إقلاع

كلّها تُنشَأ مسبقًا عبر `ensureTreeStructure()` — لا شيء يُنشَأ بتكاسل.

| المسار | ملاحظات |
|------|-----------|
| `/config/` | إعدادات عامّة للجهاز |
| `/config/notification/` | ملفّات WAV تنبيهات مشتركة (16-bit PCM، 22050Hz، مونو) |
| `/apps/` | مجلّد لكل أمر |
| `/apps/trackme/` | سجلّ الجلسة، القائمة البيضاء، signatures.csv |
| `/apps/eviltwin/` | creds.csv |
| `/apps/eviltwin/portal/` | HTML بوّابات أَسر مخصّصة |
| `/apps/wpasniff/` | wordlist.txt، `<BSSID>.cap`، cracked.csv |
| `/apps/pmkid/` | wordlist.txt، `<BSSID>.cap`، cracked.csv |
| `/apps/wifimon/` | التقاطات PCAP + probes.csv |
| `/apps/wguard/` | سجلّات CSV للجلسات |
| `/apps/bmon/` | سجلّات إعلانات BLE |
| `/apps/csidetect/` | سجلّات حضور حركة CSI |
| `/apps/fastpair/` | keys.csv، paired.csv، sniff.csv |
| `/apps/espsniff/` | التقاطات ESP-NOW (CSV + pcap) |
| `/apps/bleinfo/` | حفظ تعداد/ترصّد/إعادة GATT |
| `/apps/espchat/` | contacts.csv، config.conf |
| `/apps/badusb/scripts/` | ملفّات DuckyScript |
| `/apps/notes/` | ملفّات غطاء الملاحظات المتخفّي (`NNN.txt`) |
| `/apps/nes/roms/` | ألعاب NES (`.nes`) لمحاكي `gm` — يوفّرها المستخدم |
| `/apps/nes/states/` | حفظ حالات NES — واحدة لكل لعبة بمفتاح CRC32 |

*(القائمة الكاملة أطول — راجع النسخة الإنجليزية للجرد الكامل.)*

---

## الملفّات — تُنشَأ تلقائيًّا عند أوّل استخدام

لا تفعل شيئًا — يُنشئها البرنامج عند أوّل استخدام للميزة.

| الملفّ | يُنشِئه | يحوي |
|------|-----------|----------|
| `/wpa_supplicant.conf` | `connectwifi` عند نجاح الاتصال | بيانات اعتماد WiFi (متوافقة مع Linux) |
| `/wpa_supplicant.bak` | أوّل كتابة للعنقاء | نسخة الملفّ الأصلي |
| `/apps/README.txt` | SDCardManager | خريطة المجلّد←الأمر (لا يُستبدَل) |
| `/apps/wpasniff/<BSSID>.cap` | `wpasniff` عند التقاط EAPOL | ملفّ pcap للمصافحة |
| `/apps/wguard/001.csv` … | `wguard` — ملفّ لكل جلسة | `time,severity,rssi_dbm,message` |
| `/apps/wifimon/NNN.cap` | `wifimon` — ملفّ لكل جلسة | 802.11 PCAP خام |
| `/config/lockscreen.conf` | `lock new` / `lock timeout` / `lock boot` | تجزئة PIN + salt + مهلة + قفل عند الإقلاع (أضِف `reset=1` لمسح PIN) |
| `/config/notif.conf` | `notif` عند أي تغيير | مستويات الإشعارات + الصوت + مسارات WAV |
| `/config/clock.conf` | `tz` عند الحفظ | المنطقة الزمنية (`tz=<POSIX TZ>`) |

*(القائمة الكاملة أطول — راجع النسخة الإنجليزية.)*

---

## الملفّات — أنشئها يدويًّا (اختياري)

### `/apps/wpasniff/wordlist.txt`

مطلوب لـ: `wpasniff` ← `[c]` الكسر بقائمة مخصّصة
بدونه: يعود للقائمة المدمجة (100 كلمة، للعرض فقط)
الصيغة: كلمة مرور في كل سطر، UTF-8، بلا حدّ حجم

### `/apps/trackme/signatures.csv`

مطلوب لـ: توقيعات تتبّع مخصّصة
الصيغة: `type,company_id_hex,name,severity` — واحد في كل سطر، بلا رأس

```
BLE,0x004C,Apple AirTag,HIGH
BLE,0x0157,Samsung SmartTag,MEDIUM
```

### `/apps/beaconflood/wordlist.txt`

مطلوب لـ: `beaconflood` ← وضع `[4] file`. الصيغة: اسم SSID في كل سطر.

### `/apps/eviltwin/portal/<name>.html`

مطلوب لـ: صفحة بوّابة أَسر مخصّصة لـ `eviltwin`. مثال مصغّر:

```html
<html><body>
<form method="POST" action="/post">
  <input name="username" placeholder="Username">
  <input name="password" type="password" placeholder="Password">
  <button type="submit">Login</button>
</form>
</body></html>
```

> يجب أن تكون أسماء الحقول `username` و`password` حتى يلتقطها `eviltwin`.

---

## بيانات اعتماد WiFi والمزامنة مع Linux

`/wpa_supplicant.conf` بصيغة Linux القياسية. راجع دليل [بيانات اعتماد WiFi]({{ site.baseurl }}/wifi-credentials) لعرض كلمات المرور المحفوظة، واستيراد الشبكات من Linux، وتصديرها، وسلوك `update_config=1`.

</div>

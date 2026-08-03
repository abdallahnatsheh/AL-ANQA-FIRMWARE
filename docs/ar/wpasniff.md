---
title: التقاط WPA
permalink: /wpasniff/
lang: ar
parent: هجمات WiFi
grand_parent: WiFi
nav_order: 4
---

<div dir="rtl" markdown="1" style="text-align:right">

# التقاط WPA

## `wpasniff` / `ws` — التقاط مصافحة WPA2 وكسرها

يلتقط مصافحة WPA2 الرباعية (EAPOL M1+M2) ويكسرها اختياريًّا على الجهاز.

```
CMD> ws <index|bssid> [channel]
CMD> ws 2
CMD> ws AA:BB:CC:DD:EE:FF 6
```

### الخطوة الأولى — الالتقاط

يضبط العنقاء الراديو على وضع المراقبة على قناة الهدف، ويرسل أُطُر إلغاء مصادقة كل 4 ثوانٍ لإجبار العملاء على إعادة المصادقة.

الحالة: `[M1] waiting...` ← `[M1+M2] COMPLETE`

### الخطوة الثانية — الكسر

اضغط `c` بعد التقاط ناجح لبدء الكسر على الجهاز.

يحسب العنقاء `PBKDF2-SHA1(passphrase, SSID, 4096)` ← PMK ← PTK ← KCK ← رمز التحقّق HMAC-SHA1 MIC، ويقارنه بالـ MIC الملتقَط.

| مصدر قائمة الكلمات | المسار | السلوك |
|-----------------|------|-----------|
| قائمة كلمات SD | `/apps/wpasniff/wordlist.txt` | تُجرَّب أوّلًا، بلا حدّ للحجم |
| القائمة المدمجة | (مضمّنة) | 101 كلمة مرور WPA شائعة، تُستخدَم كخيار احتياطي |

تُحفَظ النتائج في `/apps/wpasniff/cracked.csv`. ويُكتَب ملف pcap للمصافحة في `/apps/wpasniff/<BSSID>.cap` (متوافق مع aircrack-ng / hashcat عبر hcxpcapngtool).

### المفاتيح

| المفتاح | الإجراء |
|-----|--------|
| `c` | بدء الكسر (بعد اكتمال الالتقاط) |
| `q` | إيقاف الالتقاط أو الكسر |

</div>

T-REX SD DROP-IN FILES
======================
Copy the contents of this folder onto the root of the SD card. The folder
layout already mirrors the card, so you can drag-and-drop /apps and /config.

------------------------------------------------------------------
1. apps/beaconflood/wordlist.txt   ->  E:\apps\beaconflood\wordlist.txt
------------------------------------------------------------------
~70 fake SSID names for `bf file`. One name per line, <=32 chars.
READY TO USE as-is. Run: bf file

------------------------------------------------------------------
2. config/notif.conf               ->  E:\config\notif.conf
------------------------------------------------------------------
Notification settings. The audio files are WAV, NOT mp3 (the firmware
plays raw WAV; the "mp3" name in the code is a misnomer).

REQUIRED WAV FORMAT: 16-bit PCM, 22050 Hz, mono.
Convert any sound you like:
    ffmpeg -i input.mp3 -ar 22050 -ac 1 -acodec pcm_s16le alert.wav
Then drop the .wav into E:\config\notification\ and uncomment the matching
*_file= line in notif.conf.

Free, license-clear sound sources to grab short beeps/chimes from:
  - https://freesound.org        (filter by CC0 license)
  - https://pixabay.com/sound-effects/   (royalty-free)
  - https://mixkit.co/free-sound-effects/notification/
Keep them SHORT (<1 s) so alerts don't lag the UI.

I cannot generate the .wav binaries for you (audio synthesis), so this is
the config + the exact recipe — you supply the sounds you want.

------------------------------------------------------------------
3. apps/trackme/signatures.csv     ->  E:\apps\trackme\signatures.csv
------------------------------------------------------------------
OPTIONAL extras (requires the updated firmware). The built-in tracker list
(AirTag, Tile, Samsung, Chipolo, Google, Eufy, Pebblebee + Apple handling) is
ALWAYS active. This file is MERGED on top of it — you only list extras here.

What it adds: 5 more Apple message types (AirPrint, Watch, Handoff, Tethering)
marked NONE, so iPhones/Macs/Watches doing that chatter aren't mistaken for
unknown trackers = fewer false alarms.

Format per line:  name , companyId , payloadByte , minLen , level
  companyId   = BLE manufacturer ID, hex (required)
  payloadByte = mfr-data[2] to require, hex; blank/any = match any
  minLen      = min mfr-data length; blank/0 = no check
  level       = NONE | NOTICE | WARNING | ALERT  (blank = WARNING)
                NONE = benign/suppressed.

Add your own trackers by appending lines (see examples at the bottom of the
file). Do NOT repeat the built-ins — exact duplicates are ignored.

NOTE: needs the updated firmware build. Without the file (or with no SD card),
TrackMe runs on the built-ins exactly as before.

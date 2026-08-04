# Vendored upstream: sippy/libg722

The files under `src/external/libg722/` are a verbatim subset of
[sippy/libg722](https://github.com/sippy/libg722), an implementation of
the ITU-T G.722 wideband codec.

PCMFlowG722's own code (everything outside `src/external/`) is MIT-
licensed; see the top-level [`LICENSE`](../../LICENSE). This file
reproduces the upstream copyright notices verbatim, as required by
those notices and for license-hygiene auditing.

The codec implementation itself is **Public Domain** (Steve Underwood's
contributions). The original 1993 CMU contribution is on permissive
terms with an acknowledgement request. A small amount of maintenance
by Sippy Software is under a 2-clause BSD license. All three are
MIT-compatible.

The upstream test code (Python bindings, CMake build, test harness)
is under a 2-clause BSD license and is **not** vendored here — only
the codec sources are.

---

## Verbatim upstream notices

### Steve Underwood — Public Domain (G.722 core, 2005)

```
Copyright (C) 2005 Steve Underwood

Despite my general liking of the GPL, I place my own contributions
to this code in the public domain for the benefit of all mankind -
even the slimy ones who might try to proprietize my work and use it
to my detriment.
```

### Carnegie Mellon University — permissive notice (1993)

```
Copyright (c) CMU    1993

Computer Science, Speech Group
Chengxiang Lu and Alex Hauptmann

The Carnegie Mellon ADPCM program is Copyright (c) 1993 by Carnegie Mellon
University. Use of this program, for any research or commercial purpose, is
completely unrestricted. If you make use of or redistribute this material,
we would appreciate acknowlegement of its origin.
```

### Sippy Software — 2-clause BSD (maintenance, 2014-2025)

```
Copyright (c) 2014-2025 Sippy Software, Inc., http://www.sippysoft.com
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
SUCH DAMAGE.
```

### CMake build glue (Phil Schatzmann, 2022)

```
@file g722_codec.h
@author Phil Schatzmann
@brief Include for encoder and decoder which supports C++
@version 0.1
@date 2022-05-08
@copyright Copyright (c) 2022
```

(Distributed under the upstream repository's licensing terms, i.e.
public domain / BSD-2 / CMU permissive depending on the file.)

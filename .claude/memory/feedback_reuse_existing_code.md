---
name: Reuse existing code before creating new implementations
description: Never duplicate logic or create new functions when an existing method already does the job
type: feedback
---

Always check what already exists in the class/file before writing new code. If a method, formula, or helper already does the job, call it — don't reimplement the same logic under a new name.

**Why:** In this project the user caught me creating a raw ADC `getPct()` with a duplicated voltage formula when `voltageToPercentage(bl.getBatteryVolts())` already existed in the same class and did exactly the same thing. Wasted tokens and added confusion.

**How to apply:**
- Before writing a new function, read the existing class members and methods first
- If the logic is already there (even private), call it or expose it minimally — don't copy the formula
- The right fix was one line: `return (int)voltageToPercentage(bl.getBatteryVolts());`
- This applies especially to utility/math functions: percentage calculations, unit conversions, string formatting

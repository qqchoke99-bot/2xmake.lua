# Confirmed RE (Termux/radare2 + user dump)

## Hook target (function body, NOT PLT)
Function start: 0x110CB1D0
  prologue: sub sp,#0x70 ; stp x29,x30 ; ... mov x19, x0

Call site: 0x110CB390
  bl PLT playSound (0x1298F5A0 is PLT only)

After success:
  ldr x0, [sp,#0x20]     ; FMOD::Channel*
  str x0, [x19,#0x78]    ; store on object (x19 = this)
  ... set3DAttributes / setVolume / setPitch using Channel*

## Object (x19) fields seen in asm only
+0x78  Channel*
+0x90  volume (float) loaded before setVolume
+0x94  pitch (float)
+0x98  3D data (d0)
+0xa0  3D data (s1)
+0xac  flag byte
+0xbc  word loaded at entry

## Levi
pl::memory::hook(target, detour, original**, priority)
PLGetModRegistration / load enable disable

# Sound Physics Lite — สรุปสำหรับผู้ใช้

## ต้องการ effect เสียงจริง ต้องทำอะไร

1. มีคอมพิวเตอร์
2. ติดตั้ง Android NDK + CMake
3. ใช้ template Levi:
   https://github.com/QYCottage/levilauncher-android-mod-template
4. ใส่ source จาก zip นี้ แล้ว build เป็น libSoundPhysicsLite.so
5. แพ็กเป็น .levipack (manifest + .so + icon) แล้ว import ใน Levi

## ข้อมูล RE ที่ใช้ในโค้ด

เป้าหมาย hook (จาก AI — ยังต้องยืนยันด้วย Ghidra):
- SoundSystemFMOD::play3d  (แนะนำ)
- FMOD set3DAttributes PLT
- Channel* หลัง playSound ที่ object+0x78 (candidate เก่า อาจ shift)

Effect ที่โค้ดพร้อมทำเมื่อได้ Channel*:
- setVolume ตามระยะ
- setPitch เล็กน้อย
- Set3DOcclusion / SetLowPassGain / SetReverbProperties จาก libfmod.so

## ไฟล์ .levipack ที่โหลดได้ตอนนี้

เป็น stub เท่านั้น — โหลดใน Levi ได้ แต่ยังไม่มีเสียงเปลี่ยน

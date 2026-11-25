# customskin037 (AML plugin)

Build: `ndk-build` (NDK r27 asli; `ndk-r27.zip` di bundle hanya stub 16 KB) dengan Android.mk + Application.mk di folder ini.
Hasil: `libs/armeabi-v7a/libcustomskin037.so` -> taruh di folder mod AML.

Isi `<Android/data/<paket>/files>/customskin/skins.ini`:
    20001=5,skin_a.dff,skin_a.txd     # newid=baseSkin,dff,txd

Sudah ada : remap id skin 20001-30000 -> skin vanilla di dua titik libGTASA (RequestModel, CPed::SetModelIndex).
Belum ada : (1) hook libsamp.so (validasi/RPC skin), (2) downloader dari server 0.3DL,
            (3) menempelkan DFF custom ke satu ped, (4) belum pernah dikompilasi/diuji.

#!/bin/sh
set -e
# needs mingw-w64 (x86_64 + i686 g++). Libraries are in third_party/.
cd "$(dirname "$0")"
IM=third_party/imgui
MH=third_party/minhook
mkdir -p obj obj32 out
# ---------- in-game overlay (32-bit dll, the game is 32-bit)
C32=i686-w64-mingw32-g++
F32="${EXTRA} -DLL_OVERLAY -DNDEBUG -O2 -std=c++17 -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX -I$IM -I$IM/backends -Ithird_party/stb -I$MH/include"

for f in imgui imgui_draw imgui_tables imgui_widgets; do [ -f obj32/$f.o ] || $C32 $F32 -c $IM/$f.cpp -o obj32/$f.o; done
for f in imgui_impl_win32 imgui_impl_dx11; do [ -f obj32/$f.o ] || $C32 $F32 -c $IM/backends/$f.cpp -o obj32/$f.o; done
for f in buffer hook trampoline; do [ -f obj32/mh_$f.o ] || i686-w64-mingw32-gcc -O2 -I$MH/include -c $MH/src/$f.c -o obj32/mh_$f.o; done
[ -f obj32/mh_hde32.o ] || i686-w64-mingw32-gcc -O2 -c $MH/src/hde/hde32.c -o obj32/mh_hde32.o
$C32 $F32 -c src/ui.cpp -o obj32/ui.o
$C32 $F32 -c src/settings.cpp -o obj32/settings.o
$C32 $F32 -c overlay/overlay.cpp -o obj32/overlay.o
i686-w64-mingw32-windres overlay/overlay.rc -O coff -o obj32/overlay.res
$C32 -shared -static -s -o out/LoadoutOverlay.dll obj32/*.o obj32/overlay.res -ld3d11 -ld3dcompiler -ldxgi -ldwmapi -lshell32 -lole32 -luuid -lgdi32 -luser32 -limm32 -lwinhttp -lcrypt32
# ---------- launcher (64-bit exe)
CXX=x86_64-w64-mingw32-g++
FL="${EXTRA} -DNDEBUG -O2 -std=c++17 -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX -I$IM -I$IM/backends -Ithird_party/stb"
for f in imgui imgui_draw imgui_tables imgui_widgets; do [ -f obj/$f.o ] || $CXX $FL -c $IM/$f.cpp -o obj/$f.o; done
for f in imgui_impl_win32 imgui_impl_dx11; do [ -f obj/$f.o ] || $CXX $FL -c $IM/backends/$f.cpp -o obj/$f.o; done
$CXX $FL -c src/main.cpp -o obj/main.o
$CXX $FL -Ithird_party/miniz -c src/game.cpp -o obj/game.o
$CXX $FL -c src/ui.cpp -o obj/ui.o
$CXX $FL -c src/settings.cpp -o obj/settings.o
$CXX $FL -Ithird_party/miniz -c src/setup.cpp -o obj/setup.o
[ -f obj/mz.o ] || x86_64-w64-mingw32-gcc -O2 -DNDEBUG -Ithird_party/miniz -c third_party/miniz/miniz.c -o obj/mz.o
[ -f obj/mz_zip.o ] || x86_64-w64-mingw32-gcc -O2 -DNDEBUG -Ithird_party/miniz -c third_party/miniz/miniz_zip.c -o obj/mz_zip.o
[ -f obj/mz_tinfl.o ] || x86_64-w64-mingw32-gcc -O2 -DNDEBUG -Ithird_party/miniz -c third_party/miniz/miniz_tinfl.c -o obj/mz_tinfl.o
[ -f obj/mz_tdef.o ] || x86_64-w64-mingw32-gcc -O2 -DNDEBUG -Ithird_party/miniz -c third_party/miniz/miniz_tdef.c -o obj/mz_tdef.o
x86_64-w64-mingw32-windres app.rc -O coff -o obj/app.res
$CXX -static -mwindows -municode -s -o out/LoadoutLauncher.exe obj/*.o obj/app.res -ld3d11 -ld3dcompiler -ldxgi -ldwmapi -luxtheme -lshell32 -lole32 -loleaut32 -luuid -lgdi32 -luser32 -limm32 -lwinhttp -lcrypt32
ls -la out/

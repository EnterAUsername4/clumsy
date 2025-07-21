@Echo OFF
zig build -Dconf=Release
@Echo ON

@Echo OFF
timeout /t 5 >nul

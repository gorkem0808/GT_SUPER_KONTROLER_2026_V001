@echo off
chcp 65001 >nul
cd /d "%~dp0"
title GT_SUPER KONTROLER 2026 V001

echo [GT_SUPER KONTROLER 2026 V001] Program aciliyor...
python --version >nul 2>&1
if errorlevel 1 (
  py -3 --version >nul 2>&1
  if errorlevel 1 (
    echo Python bulunamadi. Otomatik Python 3 kurulumu aciliyor...
    call "%~dp000_PYTHON3_KUR.bat"
  )
)

python --version >nul 2>&1
if not errorlevel 1 (
  set "PY_CMD=python"
) else (
  py -3 --version >nul 2>&1
  if not errorlevel 1 (
    set "PY_CMD=py -3"
  ) else (
    echo Python hala bulunamadi. Bilgisayari yeniden baslatip tekrar dene.
    pause
    exit /b 1
  )
)

%PY_CMD% -c "import serial" >nul 2>&1
if errorlevel 1 (
  echo PySerial kuruluyor...
  %PY_CMD% -m pip install -r requirements.txt
)
%PY_CMD% GT_SUPER_KONTROLER_2026_V001.py
pause

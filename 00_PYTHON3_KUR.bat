@echo off
chcp 65001 >nul
cd /d "%~dp0"
title GT SUPER CONTROLER - Python 3 Kurulum

echo ============================================================
echo GT SUPER CONTROLER - PYTHON 3 KURULUM
echo ============================================================
echo.

echo [1/4] Python kontrol ediliyor...
python --version >nul 2>&1
if not errorlevel 1 (
  echo Python zaten kurulu:
  python --version
  goto PIP
)
py -3 --version >nul 2>&1
if not errorlevel 1 (
  echo Python zaten kurulu:
  py -3 --version
  goto PIP_PY
)

echo Python bulunamadi.
echo.

set "PY_EXE=%~dp0python-3.11.9-amd64.exe"
if exist "%PY_EXE%" (
  echo [2/4] Paket icindeki Python kurulumu bulundu:
  echo %PY_EXE%
  echo Kurulum basliyor...
  "%PY_EXE%" /quiet InstallAllUsers=0 PrependPath=1 Include_test=0 Include_pip=1 SimpleInstall=1
  goto CHECK
)

echo [2/4] Paket icinde python-3.11.9-amd64.exe yok.
echo Internet varsa resmi Python kurulum dosyasi indirilecek.
echo.
set "PY_URL=https://www.python.org/ftp/python/3.11.9/python-3.11.9-amd64.exe"
echo Indiriliyor: %PY_URL%
powershell -NoProfile -ExecutionPolicy Bypass -Command "try { [Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -Uri '%PY_URL%' -OutFile '%PY_EXE%' -UseBasicParsing } catch { exit 1 }"
if errorlevel 1 (
  echo.
  echo HATA: Python indirilemedi.
  echo Internet yoksa python-3.11.9-amd64.exe dosyasini bu klasore koyup tekrar calistir.
  pause
  exit /b 1
)

echo Kurulum basliyor...
"%PY_EXE%" /quiet InstallAllUsers=0 PrependPath=1 Include_test=0 Include_pip=1 SimpleInstall=1

:CHECK
echo [3/4] Kurulum kontrol ediliyor...
timeout /t 5 /nobreak >nul
python --version >nul 2>&1
if errorlevel 1 (
  py -3 --version >nul 2>&1
  if errorlevel 1 (
    echo.
    echo Python kuruldu ama PATH henuz guncellenmedi olabilir.
    echo Bilgisayari yeniden baslatip GT_SUPER_KONTROLER_2026_V001.bat dosyasini tekrar calistir.
    pause
    exit /b 0
  )
  goto PIP_PY
)

:PIP
echo [4/4] PySerial kuruluyor/kontrol ediliyor...
python -m pip install --upgrade pip
python -m pip install -r requirements.txt
echo.
echo TAMAM: Python 3 ve PySerial hazir.
pause
exit /b 0

:PIP_PY
echo [4/4] PySerial kuruluyor/kontrol ediliyor...
py -3 -m pip install --upgrade pip
py -3 -m pip install -r requirements.txt
echo.
echo TAMAM: Python 3 ve PySerial hazir.
pause
exit /b 0

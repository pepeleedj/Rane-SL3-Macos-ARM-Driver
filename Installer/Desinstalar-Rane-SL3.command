#!/bin/bash
set -e

clear
echo "=========================================================="
echo "   DESINSTALADOR DRIVER RANE SL3                          "
echo "=========================================================="
echo ""
echo "Se eliminaran el driver SL3Device.driver, Sl3Api.framework y el panel de control."
echo "(Te pedira la contrasena de inicio de sesion de tu Mac)"
echo ""

sudo rm -rf "/Library/Audio/Plug-Ins/HAL/SL3Device.driver"
sudo rm -rf "/Library/Frameworks/Sl3Api.framework"
sudo rm -rf "/Library/PreferencePanes/SL 3 Audio Control Panel.prefPane"
sudo rm -rf "/Library/Extensions/Sl3Driver.10.9.kext"
sudo rm -f "/usr/local/bin/sl3panel"
sudo launchctl kickstart -kp system/com.apple.audio.coreaudiod >/dev/null 2>&1 || sudo killall coreaudiod >/dev/null 2>&1 || true

echo ""
echo "Driver desinstalado correctamente."
echo ""
read -p "Pulsa ENTER para cerrar esta ventana..."

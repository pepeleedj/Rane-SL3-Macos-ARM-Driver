#!/bin/bash
clear
echo "=========================================================="
echo "   PANEL DE CONTROL RANE SL3 — by pepelee (@pepeleedj)    "
echo "   Instagram: https://www.instagram.com/pepeleedj/        "
echo "=========================================================="
echo ""
if [ -x "/usr/local/bin/sl3panel" ]; then
    /usr/local/bin/sl3panel status
else
    echo "La herramienta sl3panel aun no esta instalada en /usr/local/bin/sl3panel."
    echo "Ejecuta primero Instalar-Rane-SL3.command o RaneSL3-macOS-Universal.pkg"
fi
echo ""
read -p "Pulsa ENTER para cerrar esta ventana..."

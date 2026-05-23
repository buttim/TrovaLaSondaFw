echo [1m************ compilazione per Heltec ************[0m
arduino-cli compile -e --fqbn Heltec-esp32:esp32:heltec_wifi_lora_32_V3
python3 -m esptool --chip esp32s3 merge-bin --flash-size 4MB 0x1000 "build/Heltec-esp32.esp32.heltec_wifi_lora_32_V3/TrovaLaSondaFw.ino.bootloader.bin" 0x8000 "build/Heltec-esp32.esp32.heltec_wifi_lora_32_V3/TrovaLaSondaFw.ino.partitions.bin" 0xe000 "$HOME/.arduino15/packages/Heltec-esp32/hardware/esp32/3.0.2/tools/partitions/boot_app0.bin" 0x10000 "build/Heltec-esp32.esp32.heltec_wifi_lora_32_V3/TrovaLaSondaFw.ino.bin" -o build/Heltec-esp32.esp32.heltec_wifi_lora_32_V3/TrovaLaSondaFw.ino.merged.bin

echo [1m************ compilazione per TTGO ************[0m
arduino-cli compile -e --fqbn esp32:esp32:ttgo-lora32
echo .
echo .
echo .
echo ---------------------------------------------
echo [92mAggiorna i file JSON!![0m
echo [96mUsa 'ftp -s:ftp.txt' per caricare i bin[0m
echo ---------------------------------------------

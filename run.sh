#!/system/bin/sh
# Run from MT Manager as ROOT. Put libcheat.so in the SAME folder as this script.
PKG=com.standrise.game
DIR="$(cd "$(dirname "$0")" && pwd)"
SRC="$DIR/libcheat.so"
DST=/data/local/tmp/libcheat.so

[ "$(id -u)" = "0" ] || { echo "Need root: run this script with root in MT Manager"; exit 1; }
[ -f "$SRC" ] || { echo "libcheat.so not found in $DIR"; exit 1; }

cp "$SRC" "$DST" && chmod 755 "$DST"
chcon u:object_r:apk_data_file:s0 "$DST" 2>/dev/null
setenforce 0
chmod 666 /dev/input/event* 2>/dev/null          # lets the menu read touches
[ -f /data/local/tmp/mod_rot ] || echo 1 > /data/local/tmp/mod_rot   # 1=landscape; if taps are mirrored put 3
am force-stop $PKG
setprop wrap.$PKG "LD_PRELOAD=$DST"
sleep 1
monkey -p $PKG -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
echo "Launched $PKG. Log: logcat -s mod"

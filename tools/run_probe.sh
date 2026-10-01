#!/system/bin/sh
# run_probe.sh - 在设备上通过 app 内部类还原全局文件密钥(在线密钥派生)
#
# 原理: 用 app 自己的类(com.oplus.encryption.common.utils.crypto.secretkey.b)
#       来完成 "用 AndroidKeyStore 中的 file_encryption_key 解密 encrypted_file_secret_key"
#       这一步。KeyStore 密钥受 TEE 保护无法直接导出, 但 app 进程内可调用,
#       因此通过注入类的方式让 app 自己吐出解密结果。需要 root(shizuku)。
#
# 使用前必须按你的设备调整三个占位符:
#   APK       = com.oplus.encryption 的安装路径, 形如:
#               /data/app/~~随机==/com.oplus.encryption-随机==/base.apk
#               (用 pm path com.oplus.encryption 查询)
#   UID       = 该 app 的 uid, 形如 10249 (用 dumpsys package 查询)
#   PROBE_DEX = 由 probe.smali 编译出的 dex 所在目录
#
# 依赖: probe.dex(编译 probe.smali), runas(setuid 桥, 见 runas.c)
# 用法: sh run_probe.sh "encrypted_file_secret_key 串"
#       输出以 VAULTKEYB64: 开头的那行就是 <base64 filekey>, 再配合 derive_key.py
#       或直接取 hex 交给 vaultdec。

# ---- 必改占位符 ----
APK=/data/app/__PKGNAME__/com.oplus.encryption-__RANDOM__/base.apk
PROBE_BIN=/data/local/tmp/probe/probe.dex
UID=10249
# --------------------

export ANDROID_ROOT=/system
export ANDROID_DATA=/data
export ANDROID_ART_ROOT=/apex/com.android.art
export ANDROID_I18N_ROOT=/apex/com.android.i18n
export ANDROID_TZDATA_ROOT=/apex/com.android.tzdata
export CLASSPATH=$PROBE_BIN:$APK
exec /data/local/tmp/runas $UID /system/bin/app_process -Xhidden-api-policy:disabled \
    /data/local/tmp/probe com.oplus.encryption.common.utils.crypto.secretkey.Probe "$1"

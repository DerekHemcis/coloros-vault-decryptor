# tools/ — 密钥派生辅助工具

这一组工具用于**还原 16 字节全局文件密钥**（`filekey`），它是解密所有保险箱文件
的前提。整个保险箱的所有文件共用同一个 `filekey`。

## 为什么需要这些工具

App 在 SharedPreferences 里存的密钥是**二次加密**的：

```
encrypted_file_secret_key =
    <base64_ciphertext> %IV1% <base64_iv>
```

其中 `ciphertext` 是 `filekey` 用 AndroidKeyStore 中名为 `file_encryption_key`
的密钥（16 字节），以 **AES/CTR/NoPadding** 加密后的密文。

AndroidKeyStore 的密钥受设备 TEE/HardwareKeyStore 保护，**无法从外部导出**。
但 App 自己进程内可以调用它。因此密钥派生必须"借用" App 自身的代码：

1. 用 `probe.smali` 注入到 App 进程，调用其内部类
   `com.oplus.encryption.common.utils.crypto.secretkey.b` 的 `k()` 和 `c()`，
   让它完成这次 AES 解密，吐出 `filekey` 的 base64。
2. 或者用 `derive_key.py`（见 ../src/）在"已经拿到 file_encryption_key 的明文"
   的假设下做纯解密——主要用来在 PC 上复现 / 验证。

## 各文件用途

| 文件            | 说明 |
|-----------------|------|
| `probe.smali`   | 注入用的 smali 类。调用 App 内部类完成 keystore 解密 |
| `run_probe.sh`  | 在设备上以 App 的 uid 运行 app_process 加载 probe（需 root，**需改占位符**） |
| `runas.c`       | 极小 setuid 桥，让 root 进程降权到目标 uid 后再 exec（兼容 mksh/sh） |

## probe.smali 对应的高层语义

`probe.smali` 的 `main()` 等效于以下 Java 逻辑：

```java
public static void main(String[] args) {
    try {
        AndroidKeyStoreProvider.install();
        SecretKey fileKey = com.oplus.encryption....secretkey.b.k();   // 取 keystore 密钥
        String fileKeyB64 = com.oplus.encryption....secretkey.b.c(fileKey, args[0]);
        // args[0] = encrypted_file_secret_key 串("<b64>%IV1%<b64>")
        // 打印结果, 带 VAULTKEYB64: 前缀
        System.out.println("VAULTKEYB64:" + fileKeyB64);
    } catch (Throwable t) {
        System.out.println("VAULTPROBE_ERR:" + android.util.Log.getStackTraceString(t));
    }
}
```

## run_probe.sh 占位符说明

运行前必须按你的设备替换三处：

| 占位符               | 如何获得 |
|----------------------|----------|
| `__PKGNAME__` / `__RANDOM__`（APK 路径） | `adb shell pm path com.oplus.encryption` |
| `UID = 10249`        | `adb shell dumpsys package com.oplus.encryption \| grep -i userid` |
| `PROBE_BIN` 路径     | 你放置 `probe.dex` 的路径 |

`probe.dex` 由 `probe.smali` 编译而来（`smali` + `d8`，见仓库根 README 的构建章节）。

# coloros-vault-decryptor

> 逆向分析 **ColorOS / OPPO 私密保险箱**（`com.oplus.encryption`）加密格式的解密工具链。

针对 OPPO/realme 手机自带"文件保险箱"/"私密保险箱"的加密文件格式，从 App 数据、
厂商 framework 反编译中逆向出的解密方法，整理了**工具 + 方法论**，可复现、可验证。

> ⚠️ **免责声明**
> - 本仓库**仅供技术研究**与**恢复你自己设备上丢失/遗忘进不去的数据**。
> - 你只能对**自己拥有**的设备/数据使用。解密他人数据是非法行为。
> - 仓库**不含任何设备的真实密钥、数据库、个人文件**，所有示例均为脱敏形式。
> - 使用后果自负。

## 能解决的问题

当你在 OPPO/realme 的"文件保险箱"里手动加密了一批照片/视频，但：
- 忘记了保险箱的独立密码 / 指纹不可用，
- 或 App 数据异常导致保险箱打不开，
- 而文件其实还躺在 App 的私有数据目录里没有被删除……

本工具可以**在你的设备上（需 root / Shizuku）**把这些文件解密并还原出原始目录结构。

## 它是怎么工作的（30 秒版）

```
[AndroidKeyStore file_encryption_key]   ← TEE 保护, 只能 App 进程内调
        │  AES/CTR 解密
        ▼
encrypted_key_sp.xml 的 encrypted_file_secret_key
        │  得到
        ▼
16 字节全局 filekey  ← 所有保险箱文件共用
        │  AES-128-CTR 部分解密(前1024字节) + 明文后缀原样拷贝
        ▼
还原出的 JPG/PNG/MP4… + encryption.db 还原真实文件名/目录
```

关键点：保险箱每个文件**只加密头部 1024 字节**，其余是明文；密钥虽藏在
Keystore 里，但可**借用 App 自身代码**在设备上还原。详见 [`docs/FORMAT.md`](docs/FORMAT.md)。

## 目录结构

```
coloros-vault-decryptor/
├── README.md
├── LICENSE
├── docs/
│   └── FORMAT.md          # 逆向分析：文件格式、密钥链路、数据库 schema
├── src/
│   ├── vaultdec.c         # 通用解密器 (AES-128-CTR 部分解密, 流式)
│   ├── gen_rename.py      # 依据 encryption.db 还原文件名/目录
│   └── derive_key.py      # 密钥还原(离线复现/验证部分)
└── tools/
    ├── README.md          # 密钥派生辅助工具说明
    ├── probe.smali        # 在线密钥派生探针(调用 App 内部类)
    ├── run_probe.sh       # 运行探针(需 root, 填你自己设备的值)
    └── runas.c            # setuid 降权桥
```

## 快速上手

### 你需要的东西
- 一台已 root（magisk/KernelSU）或已启用 **Shizuku** 的 OPPO/realme 手机
- 该手机上装有 `com.oplus.encryption`（系统自带，无需手动装）
- PC 上装 `python3`（用于 derive/gen_rename，可选）

### Step 1：取出需解密的数据（root）
```sh
# 以 root/shizuku 执行
adb shell
su
mkdir -p /sdcard/vault_dump
cp -r /data/data/com.oplus.encryption/databases/encryption.db \
      /data/data/com.oplus.encryption/shared_prefs/encrypted_key_sp.xml \
      /data/data/com.oplus.encryption/files/encrypted /sdcard/vault_dump/
```

### Step 2：还原全局 filekey
两种方式（任选其一）：

**A. 在线（推荐）—— 借用 App 自身代码**
按 [`tools/README.md`](tools/README.md) 编译并运行 `probe`，得到 `VAULTKEYB64:<b64 filekey>`。
将其 base64 解码为 hex，得到 32 位 hex 的 filekey。

**B. 离线复现（已有 keystore key 时）**
```sh
python3 src/derive_key.py <keystore_key_hex32> \
    '<encrypted_file_secret_key 串>'
```

### Step 3：解密
在设备上（或 PC 编译后拷到设备）：
```sh
gcc -O2 -static -o /data/local/tmp/vaultdec src/vaultdec.c -lcrypto
# 在设备内以 shell 运行:
/data/local/tmp/vaultdec <filekey_hex32> \
    /sdcard/vault_dump/encrypted/files /sdcard/vault_dump/decrypted
```
输出 `[done] ok=… skip=… fail=… magic_mismatch=…`。`magic_mismatch` 若不为 0
说明密钥或 enc_len 有误，需排查。

### Step 4：还原文件名与目录
```sh
python3 src/gen_rename.py /sdcard/vault_dump/databases/encryption.db \
    /sdcard/vault_dump/decrypted --mtimes mtimes.tsv
sh /sdcard/vault_dump/rename.sh
```
解密出的文件会按原相册/文件夹归位，并恢复成有意义的文件名（无原名的按 mtime 命名）。

## 构建与依赖

| 组件 | 构建/运行 | 依赖 |
|------|-----------|------|
| `vaultdec` | `gcc -O2 -static -o vaultdec src/vaultdec.c -lcrypto` | OpenSSL (`libssl-dev`) |
| `gen_rename.py` | `python3 src/gen_rename.py …` | Python `sqlite3`(标准库) |
| `derive_key.py` | `python3 src/derive_key.py …` | `cryptography` (pip) |
| `runas` | `gcc -O2 -static -o runas tools/runas.c` | 无 |
| `probe` | 见 `tools/README.md` | smali/baksmali, d8 |

在设备上静态编译建议用 `aarch64-musl` 工具链；在 PC 上编译后 `adb push` 到
`/data/local/tmp/` 再执行亦可（文件为流式处理，不占额外一倍空间）。

## 验证方法

- 解密后对输出文件做 **magic 嗅探**（代码内置 JPEG/PNG/MP4/WEBP/GIF/PDF…）。
- 可与 `/data/data/…/files/encrypted` 中文件逐字节 `md5` 对比还原前后一致性。
- `gen_rename.py` 会生成 `namemap.tsv`（enc_name → 目标路径），便于二次核对。

## 相关

- 同类现象也适用于某些 realme UI 的"文件保险箱"。
- 如果你发现不同 OPPO 版本加密参数（enc_len、密钥 alias）有变化，欢迎提 issue/PR。

## License

[MIT](./LICENSE)

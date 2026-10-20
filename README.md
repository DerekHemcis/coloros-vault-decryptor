# coloros-vault-decryptor

> 逆向 **ColorOS / OPPO 私密保险箱**（`com.oplus.encryption`）加密格式的记录，
> 以及一套在自己设备上把文件取回来的工具链。可复现、可验证、只描述格式与方法。

---

## 为什么会有这个仓库

Root 之后把手机环境搞坏了，厂商的保险箱独立密码失效，保险箱打不开。
十几 G 的文件全在里面，删不掉也读不出——但数据其实一直躺在
`/data/data/com.oplus.encryption/files/encrypted/`，谁也没动过。

密码校验是 App 层的事，加密文件本身是完好的。所以这不是"破解别人的保险箱"，
是一次**自救**的副产物：搞清格式 → 还原密钥 → 把文件搬出来。
工具链是这么做出来的，仓库里保留的是方法和可复现的代码。

---

## 三条关键结论（先看这个）

1. **只加密了每个文件开头的 1024 字节**，其余部分是明文原样存储。
   所以真正丢失的不是数据，是"头"和密钥。
2. **密钥是三层结构**，最后一层 `file_encryption_key` 由 TEE 保护，无法导出；
   但 App 自己进程内可以调用它——所以逆向的最后一步是"借 App 的手"。
3. **文件名和目录结构都在 `encryption.db` 里**，解密只是半件事，
   另一半是把丢掉的命名恢复回来。

---

## 文件格式

每个加密文件在磁盘上的布局：

```
┌──────────────┬──────────────────────────────┬────────────────────┐
│ IV (16 字节) │ AES-128-CTR( 明文前 N 字节 ) │ 明文剩余部分（原样） │
└──────────────┴──────────────────────────────┴────────────────────┘
```

- 算法：AES-128 / CTR / NoPadding，IV 逐文件随机、明文写在文件头。
- **N = 1024**（实测值，`vaultdec` 可用第 4 个参数调整）。
- 文件总长度 = `16 + 明文长度`，与数据库 `_size` 字段严格相等。
- 因为 CTR 是流式的 + 尾部明文，解密只需处理头部 N 字节，后面直接拷贝。

> 这是性能设计：大视频只加密头部即可满足"打不开"的诉求，
> 代价是密钥丢失后文件后半段仍可取证。对自救的人来说，这个设计就是救命的。

细节（目录、数据库 schema、边界情况）见 [`docs/FORMAT.md`](docs/FORMAT.md)。

---

## 密钥链路

```
第一层  AndroidKeyStore
        alias = "file_encryption_key"，16 字节，TEE 保护
        不可导出，只有 App 进程内能调
              │  AES-128-CTR 解密
              ▼
第二层  shared_prefs/encrypted_key_sp.xml
        encrypted_file_secret_key = <b64 密文>%IV1%<b64 IV>
        （另有 encrypted_file_backup_secret_key，结构相同，备份用途）
              │  解开后 base64 解码
              ▼
第三层  16 字节 filekey —— 整个保险箱所有文件共用一个
```

还原公式：

```python
ct, iv = [base64.b64decode(x) for x in secret_key_string.split('%IV1%')]
filekey = AES_CTR_decrypt(key=keystore_key, iv=iv, data=ct)   # 16 字节
```

第一层拿不到，所以第二层的解密必须由 App 自己做。这就是 `tools/` 存在的原因。

---

## 逆向是怎么走的（证据 → 结论）

不写"我当时试了哪些错路"（那部分没留记录），只写**能复现的证据链**：
每一步都指向下一步该看哪里。

| 观察到的事实 | 得出的结论 |
|---|---|
| 私有目录里只有 `databases/`、`shared_prefs/`、`files/encrypted/` 三处有用 | 备份这三样就等于备份了全部线索 |
| 加密文件都以 16 字节随机数据开头，且 `文件长 − 16 = db._size` | 前 16 字节是 IV，正文与明文等长 |
| 把第 1024 字节之后的数据直接按原始字节看，能命中 JPEG/PNG/MP4 的 magic | 只加密了头部 → **partial encode** |
| `encrypted_file_secret_key` 用 `%IV1%` 分隔成两段 base64 | 一段密文 + 一段 IV，说明外面还套了一层对称解密 |
| 反编译定位到 `com.oplus.encryption.common.utils.crypto.secretkey.b` 的 `k()` / `c()` | 拿到算法：AES/CTR/NoPadding，密钥来自 Keystore |
| Keystore 里的别名无法导出 | 只能换思路：让 App 自己调，见下 |

最后一步是本项目的关键：用 `app_process` 以 **App 的 uid** 加载一个注入类
（`tools/probe.smali`），在里面调用 App 自己的 `k()` / `c()`，
让它用那把不可导出的密钥完成解密，再把结果 `VAULTKEYB64:` 打印出来。
`tools/runas.c` 是个极小的 setuid 桥，让 root 进程降权到 App uid 再 exec
（有些机器的 `mksh` 不支持直接降权 exec）。

---

## 工具链

```
coloros-vault-decryptor/
├── docs/FORMAT.md       # 文件格式、密钥链路、数据库 schema
├── src/
│   ├── vaultdec.c       # 解密器：AES-128-CTR 部分解密，流式，可断点续跑
│   ├── gen_rename.py    # 按 encryption.db 还原文件名与目录
│   └── derive_key.py    # 拿到 keystore_key 之后的纯密钥还原（PC 复现/验证用）
└── tools/
    ├── probe.smali      # 注入类：借 App 之手解出 filekey
    ├── run_probe.sh     # 在设备上以 App uid 运行探针（需 root，改 3 处占位符）
    └── runas.c          # setuid 降权桥
```

| 组件 | 构建 | 依赖 |
|---|---|---|
| `vaultdec` | `gcc -O2 -static -o vaultdec src/vaultdec.c -lcrypto` | OpenSSL |
| `gen_rename.py` | 直接跑 | Python `sqlite3`（标准库） |
| `derive_key.py` | 直接跑 | Python `cryptography` |
| `runas` | `gcc -O2 -static -o runas tools/runas.c` | 无 |
| `probe` | 见 `tools/README.md`（smali + d8） | smali/baksmali, d8 |

设备侧建议用 `aarch64-musl` 静态编译；PC 上编译后 `adb push` 到
`/data/local/tmp/` 也能跑。全程流式，不会临时多占一份空间。

---

## 复现步骤

**前提**：一台已 root（Magisk / KernelSU）或启用 Shizuku 的 OPPO/realme 设备，
系统自带 `com.oplus.encryption`；PC 上可选装 `python3`。

### 1. 备份线索（root）

```sh
su
mkdir -p /sdcard/vault_dump
cp -r /data/data/com.oplus.encryption/databases/encryption.db \
      /data/data/com.oplus.encryption/shared_prefs/encrypted_key_sp.xml \
      /data/data/com.oplus.encryption/files/encrypted /sdcard/vault_dump/
```

### 2. 还原 filekey

**A. 在线（推荐）** —— 借 App 自己的代码：

```sh
sh tools/run_probe.sh '<encrypted_file_secret_key 串>'
# 输出 VAULTKEYB64:<base64>，解码得到 32 位 hex 的 filekey
```

**B. 离线（已经有 keystore_key 的明文时）**：

```sh
python3 src/derive_key.py <keystore_key_hex32> '<encrypted_file_secret_key 串>'
```

### 3. 解密

```sh
gcc -O2 -static -o /data/local/tmp/vaultdec src/vaultdec.c -lcrypto
/data/local/tmp/vaultdec <filekey_hex32> \
    /sdcard/vault_dump/encrypted/files /sdcard/vault_dump/decrypted
```

### 4. 还原文件名与目录

```sh
python3 src/gen_rename.py /sdcard/vault_dump/databases/encryption.db \
    /sdcard/vault_dump/decrypted --mtimes mtimes.tsv
sh /sdcard/vault_dump/rename.sh
```

---

## 怎么确认解对了

- `vaultdec` 结尾会打印
  `[done] ok=… skip=… fail=… magic_mismatch=… bytes=… GB`。
  **`magic_mismatch` 不为 0 就说明密钥或 `enc_len` 错了**，不是文件坏了。
- 解密后对输出做 magic 嗅探（内置 JPEG / PNG / MP4 / WEBP / GIF / PDF / OTF 判断）。
- `gen_rename.py` 会产出 `namemap.tsv`（加密名 → 目标路径），可逐条对照。
- 想更硬核：拿还原后的文件与源文件逐字节 `md5` 比对。

---

## 边界情况（实测）

- 小文件（≤ `enc_len`）会被整体加密，没有明文尾巴。
- `_display_name` 为空或只有哈希时，用 mtime 生成 `IMG_YYYYMMDD_HHMMSS`。
- 同名冲突自动加 `(1)`、`(2)`。
- `vaultdec` 支持断点续跑：输出大小正确的文件会跳过（`skip`），中途失败不用从头来。

---

## 已知局限

- **只在一台设备的某一个 ColorOS 版本上验证过**。`enc_len`、KeyStore 别名、
  `shared_prefs` 的键名都可能随版本变化——遇到解不对，先怀疑这三处。
- 逆向过程中用到的类名/方法名（`secretkey.b#k()` / `#c()`）同样与版本绑定。
- 一个未验证的推测：root 之后 TEE 的认证状态改变，可能正是"保险箱密码失效"
  的原因之一。这条没有做过对照实验，写在这里只是给后来的人一个方向。

---

## 合规

- 本仓库**仅供技术研究**，以及**恢复你自己设备上丢失或无法访问的数据**。
- 只能对**你自己拥有**的设备/数据使用。解密他人数据是违法的。
- 仓库**不含任何真实密钥、数据库、个人文件**，所有示例均为脱敏形式。
- 使用后果自负。

---

## License

[MIT](./LICENSE)

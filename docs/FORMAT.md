# 逆向分析：ColorOS 私密保险箱 (com.oplus.encryption)

本文档记录对一个真实设备的逆向结论，供研究者与遇到同样困境的用户参考。
只描述**格式与算法方法论**，不含任何设备实体数据。

## 1. 关键文件位置（需 root / shizuku）

```
/data/data/com.oplus.encryption/
├── databases/
│   └── encryption.db            # SQLite: 文件清单、真实文件名、文件夹映射
├── shared_prefs/
│   └── encrypted_key_sp.xml     # 二次加密的全局密钥(详见第 3 节)
└── files/encrypted/
    ├── files/                   # 加密文件(encryption_name = 32hex.扩展名)
    └── encryption_temp/         # 临时文件(通常为空)
```

## 2. 加密文件格式

每个加密文件在磁盘上的布局为：

```
┌──────────────┬──────────────────────────────┬──────────────────┐
│  IV (16 字节) │ AES-128-CTR( 明文前 N 字节 )  │ 明文剩余部分(原样) │
└──────────────┴──────────────────────────────┴──────────────────┘
```

- 文件总长度 = `16 + 明文长度`，与数据库 `_size` 字段**严格相等**。
- **只加密文件开头 N 字节**（实测 N = 1024），其余数据以明文存储。
- 算法：AES-128，模式 CTR，NoPadding。IV 逐文件随机，明文写在文件头。
- 因为是 CTR 流式 + 明文后缀，解密时只需对前 N 字节做解密、后续原样拷贝。

> 这个"partial encode"设计是性能优化：大视频只加密头部即可满足隐私诉求，
> 但也意味着若丢失密钥，文件后半段仍是明文可读——可被取证。

## 3. 密钥派生链路（三层）

```
第一层: AndroidKeyStore
        alias = "file_encryption_key"
        16 字节, 受 TEE 保护, 无法导出。仅 App 进程内可调用。

第二层: encrypted_key_sp.xml
        encrypted_file_secret_key =
            <base64 ciphertext> %IV1% <base64 iv>
        其中 ciphertext = AES-CTR(key=file_encryption_key, iv=base64(iv)) 加密 filekey

第三层(最终 16 字节 filekey, 所有文件共用):
        filekey = AES-CTR-Dec( key = file_encryption_key, iv )
        filekey 即 vaultdec 所需的全局密钥。
```

还原流程等价于：

```python
import base64
from Cryptodome.Cipher import AES
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

parts = secret_key_string.split('%IV1%')          # ['<b64 ct>', '<b64 iv>']
ct, iv = base64.b64decode(parts[0]), base64.b64decode(parts[1])
dec = Cipher(algorithms.AES(keystore_key), modes.CTR(iv)).decryptor()
filekey = dec.update(ct) + dec.finalize()         # 16 字节 全局文件密钥
```

其中 `keystore_key` 需在设备上通过注入 App 获得（见 `tools/`）。

## 4. encryption.db 结构（相关表）

数据库为 App 私有 SQLite 文件。与文件名还原相关的表/字段：

- `folder_table`: `_id`, `folder_name` —— 相册/文件夹名
- `file`: `encryption_name`, `_display_name`, `folder_id`, `_size`, `deleted`
  - `encryption_name`：磁盘上的实际文件名（32 位 hex + 扩展名）
  - `_display_name`：用户可见的真实文件名（可能是空/哈希，此时需按 mtime 命名）
  - `folder_id` → `folder_table._id`：文件归属的文件夹

还原步骤用一个脚本即可完成：

```sql
select encryption_name, _display_name, folder_id, _id
from file           where deleted = 0   order by _id;
select _id, folder_name from folder_table;
```

## 5. 端到端流程

```
[设备, 需 root/shizuku]
1. 拷贝关键文件:
   /data/data/com.oplus.encryption/{databases,shared_prefs,files/encrypted}
2. 在线取得 filekey (tools/probe + run_probe.sh, 或 App 内调用 keystore)
   -> 16 字节 filekey
3. 解密:
   vaultdec <filekey_hex32> <encrypted/files> <out_dir> [enc_len=1024]
4. 还原文件名/目录:
   python3 gen_rename.py <encryption.db> <out_dir> --mtimes mtimes.tsv
   sh rename.sh
```

## 6. 一些边界情况（实测）

- 小文件（≤ 加密头长度）会被整体加密，`enc` 截断为正文长度。
- 无 `_display_name`（仅哈希）的文件，可用文件修改时间生成 `IMG_YYYYMMDD_HHMMSS` 名。
- 同名冲突会自动追加 `(1)`, `(2)`。
- magic 嗅探（JPEG/PNG/MP4/WEBP…）可校验解密是否成功。

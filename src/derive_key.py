#!/usr/bin/env python3
"""
derive_key.py - 从 encrypted_key_sp.xml 还原 ColorOS 保险箱全局文件密钥

背景(逆向结论):
  app 的 SharedPreferences 里存的是两个被二次加密的密钥串, 格式统一为:
      <base64_ciphertext>%IV1%<base64_iv>
  其中 ciphertext 是"全局文件密钥"(16 字节)用 AndroidKeyStore 中的
  file_encryption_key(16 字节)以 AES/CTR/NoPadding 加密后的结果。

  即:
      filekey = AES-CTR-Dec(key = keystore_key, iv = base64(iv)).
                (base64(ciphertext))

  整个保险箱的所有文件共用同一个 filekey, 每个文件的 IV 随机且明文写在
  加密文件头 16 字节。拿到 filekey 后即可用 vaultdec 解密所有文件。

  keystore_key(file_encryption_key) 无法从 Android 外部直接导出(受 KeyStore
  TEE 保护), 需要设备上有 root/同 uid 时, 通过注入 app 进程或调用其内部类
  (见 tools/probe.smali) 让 app 自己用它来做这次 AES 解密并吐出结果。

  本脚本实现其中"拿到 keystore_key 之后"的纯密钥还原部分, 也可用于在 PC 上
  复现验证。keystore_key 的在线获取方式见 README "密钥派生" 章节。

用法:
  python3 derive_key.py <keystore_key_hex32> '<encrypted_file_secret_key串>'
  或
  echo -n "$KEY_STRING" | python3 derive_key.py <keystore_key_hex32>
"""
import sys, base64
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

def derive(keystore_key_hex, key_string):
    try:
        key = bytes.fromhex(keystore_key_hex)
    except ValueError:
        sys.exit('error: keystore_key_hex 必须是 32 个 hex 字符')
    if len(key) != 16:
        sys.exit('error: keystore_key 必须为 16 字节(32 hex)')

    parts = key_string.split('%IV1%')
    if len(parts) != 2:
        sys.exit('error: 密钥串格式不符, 应为 <b64密文>%%IV1%%<b64 IV>')
    ct, iv = base64.b64decode(parts[0]), base64.b64decode(parts[1])
    if len(iv) != 16:
        sys.exit('error: IV 必须为 16 字节')

    dec = Cipher(algorithms.AES(key), modes.CTR(iv)).decryptor()
    filekey = dec.update(ct) + dec.finalize()
    return filekey

def main():
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    key_hex = sys.argv[1]
    key_string = sys.argv[2] if len(sys.argv) == 3 else sys.stdin.read().strip()
    fk = derive(key_hex, key_string)
    print('filekey hex:', fk.hex())
    print('filekey b64:', base64.b64encode(fk).decode())

if __name__ == '__main__':
    main()

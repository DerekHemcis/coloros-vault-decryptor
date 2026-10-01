/*
 * vaultdec - ColorOS 私密保险箱 (com.oplus.encryption) 文件解密器
 *
 * 逆向得到的文件格式:
 *   [16 字节 IV][AES-128-CTR(明文头部 N 字节)][明文剩余部分(原样存储)]
 *
 * 即 "partial encode": 只加密了每个文件的开头 N 字节(默认 1024),
 * 其余数据以明文形式存储在磁盘上。文件的总长度 = 16 + 明文长度,
 * 与数据库中的 _size 字段完全对应。
 *
 * 密钥来源:
 *   app 的 SharedPreferences (encrypted_key_sp.xml) 保存两个加密密钥:
 *     encrypted_file_secret_key        = <base64 密文>%IV1%<base64 IV>
 *     encrypted_file_backup_secret_key = 同上(备份用途)
 *   用 AndroidKeyStore 中的 "file_encryption_key" 以 AES/CTR/NoPadding
 *   解密后, 得到一个 base64 串, 解码即得到 16 字节全局文件密钥。
 *   所有文件共用一个全局密钥, 每个文件的 IV 随机且以明文写在文件头。
 *
 * 使用前提:
 *   需要 root(shizuku) 读取 /data/data/com.oplus.encryption 下的文件。
 *   密钥派生见 src/keyderive/ 与 tools/probe.*(调用 app 内部类)。
 *
 * 构建(静态, musl, 在设备上以 shell 直接运行):
 *   gcc -O2 -static -o vaultdec vaultdec.c -lcrypto
 *
 * 用法:
 *   vaultdec <key_hex32> <in_dir> <out_dir> [enc_len=1024]
 *     key_hex32 : 16 字节全局文件密钥的 hex 表示(32 个 hex 字符)
 *     in_dir    : 加密文件所在目录(/data/data/com.oplus.encryption/files/encrypted/files)
 *     out_dir   : 解密输出目录(会在设备上流式写入, 避免 2 倍拷贝)
 *     enc_len   : 每个文件被加密的头部字节数(默认 1024, 可调)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <openssl/evp.h>

#define BUFSZ   (1u << 20)          /* 1 MiB 块大小 */
#define HDRLEN  16                  /* 文件开头的 16 字节 IV */

static unsigned char g_key[16];
static unsigned long long g_enclen = 1024;   /* 被加密的头部字节数 */
static int g_ok = 0, g_skip = 0, g_bad = 0, g_badmagic = 0;
static unsigned long long g_total = 0;

static int hex2bin(const char *s, unsigned char *out, int n)
{
    int i;
    if (strlen(s) != (size_t)(n * 2)) return -1;
    for (i = 0; i < n; i++) {
        unsigned int b;
        if (sscanf(s + 2 * i, "%2x", &b) != 1) return -1;
        out[i] = (unsigned char)b;
    }
    return 0;
}

/* 粗判文件类型: 用于校验解密结果是否合理(魔数是否正确) */
static const char *sniff(const unsigned char *p, size_t n)
{
    if (n >= 3 && p[0] == 0xFF && p[1] == 0xD8 && p[2] == 0xFF) return "jpeg";
    if (n >= 8 && !memcmp(p, "\x89PNG\r\n\x1a\n", 8))          return "png";
    if (n >= 12 && !memcmp(p, "RIFF", 4) && !memcmp(p + 8, "WEBP", 4)) return "webp";
    if (n >= 12 && !memcmp(p + 4, "ftyp", 4))                  return "mp4";
    if (n >= 4 && !memcmp(p, "GIF8", 4))                       return "gif";
    if (n >= 4 && !memcmp(p, "OTTO", 4))                       return "otf";
    if (n >= 4 && !memcmp(p, "\x25PDF", 4))                    return "pdf";
    return "?";
}

static void recurse(const char *in_dir, const char *out_dir);

static int decrypt_file(const char *in, const char *out)
{
    EVP_CIPHER_CTX *ctx = NULL;
    unsigned char iv[HDRLEN], *ib = NULL, *ob = NULL;
    struct stat st;
    int fi = -1, fo = -1, ret = -1, n = 0;
    unsigned long long done = 0;
    int sniffed = 0;

    if (stat(in, &st) != 0 || !S_ISREG(st.st_mode)) return 0;
    if (st.st_size <= HDRLEN) { g_skip++; return 0; }
    /* 断点续跑: 输出已存在且大小正确则跳过 */
    {
        struct stat so;
        if (stat(out, &so) == 0 && so.st_size == st.st_size - HDRLEN) { g_skip++; return 0; }
    }

    fi = open(in, O_RDONLY);
    if (fi < 0) { fprintf(stderr, "  ! open %s: %s\n", in, strerror(errno)); g_bad++; return -1; }
    if (read(fi, iv, HDRLEN) != HDRLEN) { close(fi); g_bad++; return -1; }

    fo = open(out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fo < 0) { fprintf(stderr, "  ! create %s: %s\n", out, strerror(errno)); close(fi); g_bad++; return -1; }

    ib = malloc(BUFSZ); ob = malloc(BUFSZ + 32);
    if (!ib || !ob) { fprintf(stderr, "  ! oom\n"); goto out; }

    ctx = EVP_CIPHER_CTX_new();
    if (!ctx) goto out;
    if (EVP_DecryptInit_ex(ctx, EVP_aes_128_ctr(), NULL, g_key, iv) != 1) goto out;

    {
        unsigned long long body = (unsigned long long)st.st_size - HDRLEN;
        unsigned long long enc = g_enclen;
        if (enc > body) enc = body;                 /* 小文件则整体加密 */
        while (done < enc) {                        /* 阶段1: 解密头部 */
            int olen = 0;
            size_t want = BUFSZ;
            if (enc - done < (unsigned long long)want) want = (size_t)(enc - done);
            n = read(fi, ib, want);
            if (n <= 0) goto out;
            if (EVP_DecryptUpdate(ctx, ob, &olen, ib, n) != 1) goto out;
            if (olen > 0 && write(fo, ob, olen) != olen) { fprintf(stderr, "  ! write %s\n", out); goto out; }
            done += n;
            if (!sniffed) {
                /* 首个块解密后判定 magic */
                if (sniff(ob, olen < 32 ? olen : 32)[0] == '?') { g_badmagic++; sniffed = 2; }
                else sniffed = 1;
            }
        }
        while ((n = read(fi, ib, BUFSZ)) > 0) {     /* 阶段2: 剩余明文原样拷贝 */
            if (write(fo, ib, n) != n) { fprintf(stderr, "  ! write %s\n", out); goto out; }
            done += n;
        }
    }
    if (n < 0) goto out;
    ret = 0;
    /* 保留原文件的修改时间 */
    { struct timeval tv[2]; tv[0].tv_sec = st.st_mtime; tv[0].tv_usec = 0;
      tv[1] = tv[0]; futimes(fo, tv); }
    g_ok++; g_total += done;

out:
    if (ctx) EVP_CIPHER_CTX_free(ctx);
    free(ib); free(ob);
    if (fi >= 0) close(fi);
    if (fo >= 0) { close(fo); if (ret != 0) unlink(out); }
    return ret;
}

/* 递归遍历输入目录, 在输出目录中重建目录结构 */
static void recurse(const char *in_dir, const char *out_dir)
{
    DIR *d; struct dirent *de;
    char ip[4096], op[4096];

    d = opendir(in_dir);
    if (!d) { fprintf(stderr, "opendir %s: %s\n", in_dir, strerror(errno)); return; }

    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;         /* 跳过隐藏项/./.. */
        snprintf(ip, sizeof(ip), "%s/%s", in_dir, de->d_name);
        snprintf(op, sizeof(op), "%s/%s", out_dir, de->d_name);
        if (de->d_type == DT_DIR) {
            mkdir(op, 0755);
            recurse(ip, op);
        } else {
            decrypt_file(ip, op);
        }
    }
    closedir(d);
}

int main(int argc, char **argv)
{
    int ok;

    if (argc != 4 && argc != 5) {
        fprintf(stderr, "usage: %s <key_hex32> <in_dir> <out_dir> [enc_len=1024]\n", argv[0]);
        return 2;
    }
    if (hex2bin(argv[1], g_key, 16) != 0) { fprintf(stderr, "bad key hex\n"); return 2; }
    if (argc == 5) g_enclen = strtoull(argv[4], NULL, 0);
    mkdir(argv[3], 0755);

    recurse(argv[2], argv[3]);

    fprintf(stderr, "\n[done] ok=%d skip=%d fail=%d magic_mismatch=%d bytes=%.2f GB\n",
            g_ok, g_skip, g_bad, g_badmagic, g_total / 1073741824.0);
    ok = (g_bad == 0);
    return ok ? 0 : 1;
}

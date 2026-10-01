#!/usr/bin/env python3
"""
gen_rename.py - 根据 encryption.db 生成文件重命名/归类脚本(通用版)

作用:
  解密出的文件保留的是 encryption_name(32 位 hex + 扩展名), 但数据库里保存了
  每个文件真实的显示名 _display_name 与所属文件夹 folder_table.folder_name。
  本工具读取这些字段, 生成一段 shell 脚本, 把解密文件 mv 到正确的子目录并还原
  为有意义的文件名, 处理同名冲突(自动追加 (1), (2)...) 和非法字符清理。

用法(通用, 全部参数化):
  python3 gen_rename.py <encryption.db> <src_dir> [options]

  必填参数:
    encryption.db  保险箱 app 的 SQLite 数据库路径
                     (/data/data/com.oplus.encryption/databases/encryption.db)
    src_dir        已解密文件的输出目录(将在此目录内生成重命名脚本并原地移动)

  可选参数:
    --out <path>        生成的重命名脚本路径(默认: <src_dir>/../rename.sh)
    --namemap <path>    生成 encryption_name -> 目标路径 的映射表(默认<src_dir>/../namemap.tsv)
    --mtimes <path>     可选: "enc_name<tab>mtime" 两列文件, 用于给无原名文件按修改时间命名
    --maxlen <int>      文件名最长字节数(默认 110)

  说明:
    - 无原始显示名(仅 32 hex)的文件会用文件修改时间生成 IMG_YYYYMMDD_HHMMSS 风格名。
    - 生成的脚本用 /system/bin/sh 执行(Android), 对非法文件名字符做替换。
"""
import sqlite3, re, os, time, sys, argparse

def sanitize(name, maxlen=110):
    name = name.replace('\x00', '')
    name = re.sub(r'[/\\:*?"<>|\r\n\t]', '_', name)
    name = re.sub(r'\s+', ' ', name).strip(' .')
    if len(name.encode('utf-8')) > maxlen:
        while len(name.encode('utf-8')) > maxlen:
            name = name[:-1]
        name = name.rstrip(' .')
    return name or 'file'

def main():
    ap = argparse.ArgumentParser(description='还原 ColorOS 保险箱文件名')
    ap.add_argument('db', help='encryption.db 路径')
    ap.add_argument('src', help='已解密文件目录')
    ap.add_argument('--out', default=None, help='输出重命名脚本路径')
    ap.add_argument('--namemap', default=None, help='输出 加密名->目标名 映射表')
    ap.add_argument('--mtimes', default=None, help='可选 mtimes.tsv 路径')
    ap.add_argument('--maxlen', type=int, default=110)
    args = ap.parse_args()

    DB, SRC = args.db, args.src
    OUT = args.out or os.path.join(os.path.dirname(SRC.rstrip('/')) or '.', 'rename.sh')
    MAP = args.namemap or os.path.join(os.path.dirname(SRC.rstrip('/')) or '.', 'namemap.tsv')

    con = sqlite3.connect(DB)
    folders = {r[0]: r[1] for r in con.execute("select _id, folder_name from folder_table")}

    MT = {}
    if args.mtimes and os.path.exists(args.mtimes):
        for line in open(args.mtimes, encoding='utf-8'):
            n, t = line.rstrip('\n').split('\t', 1)
            MT[n] = int(float(t))
    else:
        print('warn: no --mtimes, unnamed files get hash-based names', file=sys.stderr)

    used, planned, stats = set(), {}, {'clean': 0, 'generated': 0}

    for enc, disp, folder_id, _id in con.execute(
            "select encryption_name,_display_name,folder_id,_id from file where deleted=0 order by _id"):
        ext = enc.rsplit('.', 1)[-1] if '.' in enc else 'bin'
        sub = folders.get(folder_id, '')
        base = disp.rsplit('.', 1)[0] if '.' in disp else disp
        if re.fullmatch(r'[0-9a-f]{32}( \(\d+\))?', base):
            mt = MT.get(enc, 0)
            base = ('IMG_' + time.strftime('%Y%m%d_%H%M%S', time.localtime(mt))) if mt \
                   else ('IMG_' + enc[:16])
            stats['generated'] += 1
        else:
            stats['clean'] += 1
        base = sanitize(base, args.maxlen)
        n = 1
        cand = base
        while (sub, cand.lower() + '.' + ext.lower()) in used:
            n += 1
            cand = '%s (%d)' % (base, n)
        used.add((sub, cand.lower() + '.' + ext.lower()))
        planned[enc] = os.path.join(sub, cand + '.' + ext) if sub else cand + '.' + ext

    with open(MAP, 'w', encoding='utf-8') as f:
        for enc, dest in planned.items():
            f.write('%s\t%s\n' % (enc, dest))

    with open(OUT, 'w', encoding='utf-8') as fd:
        fd.write('#!/system/bin/sh\ncd %s || exit 1\n' % SRC)
        for sub in set(folders.values()):
            fd.write('mkdir -p %s\n' % "'%s'" % sub)
        for enc, dest in planned.items():
            src = "'%s'" % enc.replace("'", "'\\''")
            dst = "'%s'" % dest.replace("'", "'\\''")
            fd.write('[ -f %s ] && mv -f %s %s\n' % (src, src, dst))

    print('clean=%d generated=%d total=%d' % (stats['clean'], stats['generated'], len(planned)))
    print('rename script:', OUT)
    print('namemap     :', MAP)

if __name__ == '__main__':
    main()

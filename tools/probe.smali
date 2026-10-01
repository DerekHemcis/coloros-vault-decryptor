.class public Lcom/oplus/encryption/common/utils/crypto/secretkey/Probe;
.super Ljava/lang/Object;
.source "Probe.java"


.method public constructor <init>()V
    .registers 1
    invoke-direct {p0}, Ljava/lang/Object;-><init>()V
    return-void
.end method


.method public static main([Ljava/lang/String;)V
    .registers 6

    :try_start_0
    invoke-static {}, Landroid/security/keystore2/AndroidKeyStoreProvider;->install()V

    const/4 v0, 0x0
    aget-object v0, p0, v0
    invoke-static {}, Lcom/oplus/encryption/common/utils/crypto/secretkey/b;->k()Ljavax/crypto/SecretKey;
    move-result-object v1
    invoke-static {v1, v0}, Lcom/oplus/encryption/common/utils/crypto/secretkey/b;->c(Ljavax/crypto/SecretKey;Ljava/lang/String;)Ljava/lang/String;
    move-result-object v0

    new-instance v1, Ljava/lang/StringBuilder;
    invoke-direct {v1}, Ljava/lang/StringBuilder;-><init>()V
    const-string v2, "VAULTKEYB64:"
    invoke-virtual {v1, v2}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;
    invoke-virtual {v1, v0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;
    invoke-virtual {v1}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;
    move-result-object v0

    const-string v1, "VAULTPROBE"
    invoke-static {v1, v0}, Landroid/util/Log;->e(Ljava/lang/String;Ljava/lang/String;)I
    sget-object v1, Ljava/lang/System;->out:Ljava/io/PrintStream;
    invoke-virtual {v1, v0}, Ljava/io/PrintStream;->println(Ljava/lang/String;)V
    :try_end_0
    .catch Ljava/lang/Throwable; {:try_start_0 .. :try_end_0} :catch_0

    goto :done

    :catch_0
    move-exception v0
    invoke-static {v0}, Landroid/util/Log;->getStackTraceString(Ljava/lang/Throwable;)Ljava/lang/String;
    move-result-object v0
    const-string v1, "VAULTPROBE_ERR"
    invoke-static {v1, v0}, Landroid/util/Log;->e(Ljava/lang/String;Ljava/lang/String;)I
    sget-object v1, Ljava/lang/System;->out:Ljava/io/PrintStream;
    invoke-virtual {v1, v0}, Ljava/io/PrintStream;->println(Ljava/lang/String;)V

    :done
    return-void
.end method

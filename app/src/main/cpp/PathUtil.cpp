//
// Created by hbl on 2017/5/17.
//

#include <jni.h>
#include "common.h"
#include <jni.h>
#include <sys/types.h>
#include "bzip2/bzlib.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <err.h>
#include <unistd.h>
#include <fcntl.h>


static off_t offtin(u_char *buf) {
    off_t y;

    y = buf[7] & 0x7F;
    y = y * 256;
    y += buf[6];
    y = y * 256;
    y += buf[5];
    y = y * 256;
    y += buf[4];
    y = y * 256;
    y += buf[3];
    y = y * 256;
    y += buf[2];
    y = y * 256;
    y += buf[1];
    y = y * 256;
    y += buf[0];

    if (buf[7] & 0x80) y = -y;

    return y;
}

extern "C"
int applypatch(int argc, char *argv[]) {
    FILE *f, *cpf, *dpf, *epf;
    BZFILE *cpfbz2, *dpfbz2, *epfbz2;
    int cbz2err, dbz2err, ebz2err;
    int fd;
    ssize_t oldsize, newsize;
    ssize_t bzctrllen, bzdatalen;
    u_char header[32], buf[8];
    u_char *old, *neew;
    off_t oldpos, newpos;
    off_t ctrl[3];
    off_t lenread;
    off_t i;
    if (argc != 4) {
        LOGD("usage: %s oldfile newfile patchfile\n", argv[0]);
        return 1;//缺少文件路径
    }

    /* Open patch file */
    if ((f = fopen(argv[3], "r")) == NULL) {
        LOGD("fopen(%s)", argv[3]);
        return 2;//打开patch文件失败
    }

    /*
    File format:
        0	8	"BSDIFF40"
        8	8	X
        16	8	Y
        24	8	sizeof(newfile)
        32	X	bzip2(control block)
        32+X	Y	bzip2(diff block)
        32+X+Y	???	bzip2(extra block)
    with control block a set of triples (x,y,z) meaning "add x bytes
    from oldfile to x bytes from the diff block; copy y bytes from the
    extra block; seek forwards in oldfile by z bytes".
    */

    /* Read header */
    if (fread(header, 1, 32, f) < 32) {
        if (feof(f)) {
            LOGD("Corrupt patch\n");
            return 3;//读取patch文件失败
        }
        LOGD("fread(%s)", argv[3]);
        return 3;//读取patch文件失败
    }

    /* Check for appropriate magic */
    if (memcmp(header, "BSDIFF40", 8) != 0) {
        LOGD("Corrupt patch\n");
        return 3;//读取patch文件失败
    }


    /* Read lengths from header */
    bzctrllen = offtin(header + 8);
    bzdatalen = offtin(header + 16);
    newsize = offtin(header + 24);
    if ((bzctrllen < 0) || (bzdatalen < 0) || (newsize < 0)) {
        LOGD("Corrupt patch\n");
        return 3;//读取patch文件失败
    }

    /* Close patch file and re-open it via libbzip2 at the right places */
    if (fclose(f)) {
        LOGD("fclose(%s)", argv[3]);
        return 3;//读取patch文件失败
    }
    if ((cpf = fopen(argv[3], "r")) == NULL) {
        LOGD("fopen(%s)", argv[3]);
        return 3;//读取patch文件失败
    }
    if (fseeko(cpf, 32, SEEK_SET)) {
        LOGD("fseeko(%s, %lld)", argv[3], (long long) 32);
        return 3;//读取patch文件失败
    }
    if ((cpfbz2 = BZ2_bzReadOpen(&cbz2err, cpf, 0, 0, NULL, 0)) == NULL) {
        LOGD("BZ2_bzReadOpen, bz2err = %d", cbz2err);
        return 3;//读取patch文件失败
    }
    if ((dpf = fopen(argv[3], "r")) == NULL) {
        LOGD("fopen(%s)", argv[3]);
        return 3;//读取patch文件失败

    }
    if (fseeko(dpf, 32 + bzctrllen, SEEK_SET)) {
        LOGD("fseeko(%s, %lld)", argv[3], (long long) (32 + bzctrllen));
        return 3;//读取patch文件失败
    }
    if ((dpfbz2 = BZ2_bzReadOpen(&dbz2err, dpf, 0, 0, NULL, 0)) == NULL) {
        LOGD("BZ2_bzReadOpen, bz2err = %d", dbz2err);
        return 3;//读取patch文件失败
    }
    if ((epf = fopen(argv[3], "r")) == NULL) {
        LOGD("fopen(%s)", argv[3]);
        return 3;//读取patch文件失败
    }
    if (fseeko(epf, 32 + bzctrllen + bzdatalen, SEEK_SET)) {
        LOGD("fseeko(%s, %lld)", argv[3], (long long) (32 + bzctrllen + bzdatalen));
        return 3;//读取patch文件失败
    }
    if ((epfbz2 = BZ2_bzReadOpen(&ebz2err, epf, 0, 0, NULL, 0)) == NULL) {
        LOGD("BZ2_bzReadOpen, bz2err = %d", ebz2err);
        return 3;//读取patch文件失败
    }

    if (((fd = open(argv[1], O_RDONLY, 0)) < 0) ||
        ((oldsize = lseek(fd, 0, SEEK_END)) == -1) ||
        ((old = (u_char *) malloc(oldsize + 1)) == NULL) ||
        (lseek(fd, 0, SEEK_SET) != 0) ||
        (read(fd, old, oldsize) != oldsize) ||
        (close(fd) == -1)) {
        LOGD("%s", argv[1]);
        return 4;//读取旧安装包失败
    }
    if ((neew = (u_char *) malloc(newsize + 1)) == NULL) {
        return 8;//"内存分配失败"
    }

    oldpos = 0;
    newpos = 0;
    while (newpos < newsize) {
        /* Read control data */
        for (i = 0; i <= 2; i++) {
            lenread = BZ2_bzRead(&cbz2err, cpfbz2, buf, 8);
            if ((lenread < 8) || ((cbz2err != BZ_OK) &&
                                  (cbz2err != BZ_STREAM_END)))
                return 5;//合并apk失败
            ctrl[i] = offtin(buf);
        };

        /* Sanity-check */
        if (newpos + ctrl[0] > newsize)
            return 5;//合并apk失败

        /* Read diff string */
        lenread = BZ2_bzRead(&dbz2err, dpfbz2, neew + newpos, ctrl[0]);
        if ((lenread < ctrl[0]) ||
            ((dbz2err != BZ_OK) && (dbz2err != BZ_STREAM_END)))
            return 5;//合并apk失败

        /* Add old data to diff string */
        for (i = 0; i < ctrl[0]; i++)
            if ((oldpos + i >= 0) && (oldpos + i < oldsize))
                neew[newpos + i] += old[oldpos + i];

        /* Adjust pointers */
        newpos += ctrl[0];
        oldpos += ctrl[0];

        /* Sanity-check */
        if (newpos + ctrl[1] > newsize)
            return 5;//合并apk失败

        /* Read extra string */
        lenread = BZ2_bzRead(&ebz2err, epfbz2, neew + newpos, ctrl[1]);
        if ((lenread < ctrl[1]) ||
            ((ebz2err != BZ_OK) && (ebz2err != BZ_STREAM_END)))
            return 5;//合并apk失败

        /* Adjust pointers */
        newpos += ctrl[1];
        oldpos += ctrl[2];
    };

    /* Clean up the bzip2 reads */
    BZ2_bzReadClose(&cbz2err, cpfbz2);
    BZ2_bzReadClose(&dbz2err, dpfbz2);
    BZ2_bzReadClose(&ebz2err, epfbz2);
    if (fclose(cpf) || fclose(dpf) || fclose(epf))
        return 6;//清理内存失败

    /* Write the new file */
    if (((fd = open(argv[2], O_CREAT | O_TRUNC | O_WRONLY, 0666)) < 0) ||
        (write(fd, neew, newsize) != newsize) || (close(fd) == -1))
        return 7;//生成新的apk失败

    free(neew);
    free(old);

    return 0;
}
extern "C"
JNIEXPORT jint JNICALL Java_com_example_hbl_smartupdate_PatchUtil_patch
        (JNIEnv *env, jclass cls,
         jstring old, jstring neew, jstring patch
        ) {
    int argc = 4;
    char *argv[argc];
    argv[0] = "bspatch";
    argv[1] = (char *) (env->GetStringUTFChars(old, 0));
    argv[2] = (char *) ((env)->GetStringUTFChars(neew, 0));
    argv[3] = (char *) ((env)->GetStringUTFChars(patch, 0));

    LOGD("old apk = %s \n", argv[1]);
    LOGD("patch = %s \n", argv[3]);
    LOGD("new apk = %s \n", argv[2]);

    int ret = applypatch(argc, argv);

    LOGD("patch result = %d ", ret);

    (env)->ReleaseStringUTFChars(old, argv[1]);
    (env)->ReleaseStringUTFChars(neew, argv[2]);
    (env)->ReleaseStringUTFChars(patch, argv[3]);
    return ret;
}



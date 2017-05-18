package com.example.hbl.smartupdate;

import android.content.Intent;
import android.content.pm.PackageInfo;
import android.os.Bundle;
import android.os.Environment;
import android.support.v7.app.AppCompatActivity;
import android.util.Log;
import android.view.View;

import com.example.hbl.smartupdate.util.ApkUtil;
import com.example.hbl.smartupdate.util.SignUtil;

import java.io.File;

public class MainActivity extends AppCompatActivity {
    static final String TAG = MainActivity.class.getSimpleName();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        findViewById(R.id.btn_enter).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
//                update();
                startActivity(new Intent(MainActivity.this,Main2Activity.class));
            }
        });
        findViewById(R.id.btn_patch).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                downloadAndPatch();
            }
        });
    }

    private void update() {
        String sourceApkPath = ApkUtil.getSourceApkPath(this);
        String md5ByFile = SignUtil.getMd5ByFile(new File(sourceApkPath));
        PackageInfo installedApkPackageInfo = ApkUtil.getInstalledApkPackageInfo(this, getPackageName());
        int versionCode = installedApkPackageInfo.versionCode;
        Log.d(TAG, "update: md5: " + md5ByFile + "\n versionCode: " + versionCode);
    }

    private void downloadAndPatch() {
        String dir = Environment.getExternalStorageDirectory().getAbsolutePath() + File.separator;
        final String oldPatch = ApkUtil.getSourceApkPath(this);
        final String newPatch = dir + "new.apk";
        final String patchPatch = dir + "patch.patch";
        Log.d(TAG, "downloadAndPatch: "+newPatch+" / "+patchPatch+" / "+oldPatch);
        new Thread(new Runnable() {
            @Override
            public void run() {
                final int patch = PatchUtil.patch(oldPatch, newPatch, patchPatch);
                runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        switch (patch) {
                            case 0:
                                Log.d(TAG, "patch: success");
                                install(newPatch);
                                break;
                            case 1:
                                Log.d(TAG, "patch: 缺少文件路径");
                                break;
                            case 2:
                                Log.d(TAG, "patch: 打开patch文件失败");
                                break;
                            case 3:
                                Log.d(TAG, "patch: 读取patch文件失败");
                                break;
                            case 4:
                                Log.d(TAG, "patch: 读取旧安装包失败");
                                break;
                            case 5:
                                Log.d(TAG, "patch: 合并apk失败");
                                break;
                            case 6:
                                Log.d(TAG, "patch: 清理内存失败");
                                break;
                            case 7:
                                Log.d(TAG, "patch: 生成新的apk失败");
                                break;
                            case 8:
                                Log.d(TAG, "patch: 内存分配失败");
                                break;
                        }
                    }

                });
            }
        }).start();
    }


    private void install(String newPatch) {
        Log.d(TAG, "install: ");
    }
}

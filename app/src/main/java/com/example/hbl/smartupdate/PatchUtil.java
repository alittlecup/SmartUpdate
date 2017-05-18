package com.example.hbl.smartupdate;

/**
 * Created by hbl on 2017/5/17.
 */

public class PatchUtil {
    static {
        System.loadLibrary("PatchUtil");
    }
    public static native int patch(String old,String neew,String patch);
}

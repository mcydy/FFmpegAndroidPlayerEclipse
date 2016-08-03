package com.fuchao.ffmpegandroidplayer;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

import android.content.Context;
import android.opengl.GLSurfaceView;

/**
 * 显示视频的GLSurfaceView
 * 
 * @author fuchao
 * 
 */
public class GlVideoView extends GLSurfaceView implements GLSurfaceView.Renderer {

	static {
		System.loadLibrary("GlPlayer");
	}

	public GlVideoView(Context context) {
		super(context);
		// 设置渲染器
		setRenderer(this);
	}

	@Override
	public void onSurfaceCreated(GL10 arg0, EGLConfig arg1) {
		onNdkSurfaceCreated();
	}

	@Override
	public void onSurfaceChanged(GL10 arg0, int arg1, int arg2) {
		onNdkSurfaceChanged(arg1, arg2);
	}

	@Override
	public void onDrawFrame(GL10 arg0) {
		onNdkDrawFrame();
	}

	private native void onNdkSurfaceCreated();

	private native void onNdkSurfaceChanged(int width, int height);

	private native void onNdkDrawFrame();

	// 销毁NDK的资源
	private native void onNdkDestory();

}

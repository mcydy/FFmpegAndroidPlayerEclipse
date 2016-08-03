package com.fuchao.ffmpegandroidplayer;

import android.app.Activity;
import android.opengl.GLSurfaceView;
import android.os.Bundle;

public class GlPlayActivity extends Activity {

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		GlVideoView surface = new GlVideoView(this);
		setContentView(surface);
	}

}

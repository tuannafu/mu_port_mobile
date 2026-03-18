package com.main52.android

import android.app.Activity
import android.os.Bundle
import android.widget.TextView

class MainActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val label = TextView(this).apply {
            text = stringFromJNI()
            textSize = 18f
            setPadding(48, 96, 48, 48)
        }

        setContentView(label)
    }

    private external fun stringFromJNI(): String

    companion object {
        init {
            System.loadLibrary("main52_stub")
        }
    }
}

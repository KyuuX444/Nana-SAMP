package com.nana.samp.core

import android.media.AudioAttributes
import android.media.MediaPlayer

/**
 * Native Android replacement for Windows BASS.dll URL audio streaming
 * triggered by SA-MP RPC_PlayAudioStream (41) and RPC_StopAudioStream (42).
 */
class AudioStreamPlayer {

    private var player: MediaPlayer? = null
    private var currentUrl: String = ""

    fun syncState(playing: Boolean, url: String) {
        if (!playing || url.isBlank()) {
            stop()
            return
        }
        if (url == currentUrl && player != null) return
        play(url)
    }

    private fun play(url: String) {
        stop()
        currentUrl = url
        try {
            val mp = MediaPlayer()
            mp.setAudioAttributes(
                AudioAttributes.Builder()
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                    .setUsage(AudioAttributes.USAGE_GAME)
                    .build()
            )
            mp.setDataSource(url)
            mp.setOnPreparedListener { it.start() }
            mp.setOnErrorListener { _, _, _ ->
                stop()
                true
            }
            mp.prepareAsync()
            player = mp
        } catch (_: Exception) {
            stop()
        }
    }

    fun stop() {
        currentUrl = ""
        try {
            player?.stop()
        } catch (_: Exception) {
        }
        try {
            player?.release()
        } catch (_: Exception) {
        }
        player = null
    }
}

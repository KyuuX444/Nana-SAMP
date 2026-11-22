package com.nana.samp.core

/**
 * Shared SA-MP 0.3.7 / 0.3.DL / open.mp protocol constants and immutable UI models
 * consumed by SampDialogPresenter, ScoreboardDialog, TextDrawOverlayView, and ServerStore.
 */
object NativeBridge {
    const val PROTOCOL_037 = 4057
    const val PROTOCOL_03DL = 4062

    data class ChatLine(
        val rgba: Int,
        val text: String,
    )

    data class DialogModel(
        val id: Int,
        val style: Int,
        val title: String,
        val info: String,
        val button1: String,
        val button2: String,
    )

    data class ScoreboardItem(
        val id: Int,
        val name: String,
        val score: Int,
        val ping: Int,
        val rgba: Int,
        val isLocal: Boolean = false,
    )

    data class TextDrawModel(
        val id: Int,
        val flags: Int,
        val letterWidth: Float,
        val letterHeight: Float,
        val letterColorAbgr: Int,
        val lineWidth: Float,
        val lineHeight: Float,
        val boxColorAbgr: Int,
        val shadow: Int,
        val outline: Int,
        val bgColorAbgr: Int,
        val style: Int,
        val selectable: Boolean,
        val x: Float,
        val y: Float,
        val modelId: Int,
        val text: String,
    ) {
        val useBox: Boolean get() = (flags and 0x01) != 0
        val alignment: Int
            get() = when {
                (flags and 0x02) != 0 -> 1 // Left
                (flags and 0x04) != 0 -> 3 // Right
                (flags and 0x08) != 0 -> 2 // Center
                else -> 1
            }
    }

    data class TextDrawOverlayState(
        val items: List<TextDrawModel> = emptyList(),
        val selecting: Boolean = false,
        val hoverRgba: Int = 0xFFFFD700.toInt(),
    )
}

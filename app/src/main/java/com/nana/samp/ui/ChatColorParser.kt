package com.nana.samp.ui

/**
 * Pure-Kotlin SA-MP chat color segment parser supporting inline {RRGGBB} tags
 * and SA-MP RGBA (0xRRGGBBAA) -> Android ARGB (0xAARRGGBB) conversion.
 */
object ChatColorParser {

    data class Segment(val text: String, val argbColor: Int)

    fun sampRgbaToArgb(rgba: Int): Int {
        val r = (rgba ushr 24) and 0xFF
        val g = (rgba ushr 16) and 0xFF
        val b = (rgba ushr 8) and 0xFF
        val a = rgba and 0xFF
        val finalAlpha = if (a == 0) 0xFF else a
        return (finalAlpha shl 24) or (r shl 16) or (g shl 8) or b
    }

    fun parse(rawText: String, baseSampRgba: Int): List<Segment> {
        val segments = mutableListOf<Segment>()
        var currentColor = sampRgbaToArgb(baseSampRgba)
        val regex = Regex("\\{([0-9A-Fa-f]{6})\\}")

        var cursor = 0
        for (match in regex.findAll(rawText)) {
            if (match.range.first > cursor) {
                val chunk = rawText.substring(cursor, match.range.first)
                if (chunk.isNotEmpty()) {
                    segments.add(Segment(chunk, currentColor))
                }
            }
            val hex = match.groupValues[1]
            val rgb = hex.toInt(16)
            currentColor = (0xFF shl 24) or rgb
            cursor = match.range.last + 1
        }

        if (cursor < rawText.length) {
            segments.add(Segment(rawText.substring(cursor), currentColor))
        }
        if (segments.isEmpty() && rawText.isNotEmpty()) {
            segments.add(Segment(rawText, currentColor))
        }
        return segments
    }
}

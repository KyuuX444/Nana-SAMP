package com.nana.samp

import com.nana.samp.ui.ChatColorParser
import org.junit.Assert.assertEquals
import org.junit.Test

class ChatColorParserTest {

    @Test
    fun convertsSampRgbaToAndroidArgb() {
        // SA-MP 0xFF8040FF (R=FF, G=80, B=40, A=FF) -> Android 0xFFFF8040
        assertEquals(0xFFFF8040.toInt(), ChatColorParser.sampRgbaToArgb(0xFF8040FF.toInt()))
        // Zero alpha defaults to opaque
        assertEquals(0xFF112233.toInt(), ChatColorParser.sampRgbaToArgb(0x11223300))
    }

    @Test
    fun parsesInlineSampColorCodes() {
        val segs = ChatColorParser.parse("Hello {FF0000}Red {00FF00}Green", 0xFFFFFFFF.toInt())
        assertEquals(3, segs.size)
        assertEquals("Hello ", segs[0].text)
        assertEquals(0xFFFFFFFF.toInt(), segs[0].argbColor)
        assertEquals("Red ", segs[1].text)
        assertEquals(0xFFFF0000.toInt(), segs[1].argbColor)
        assertEquals("Green", segs[2].text)
        assertEquals(0xFF00FF00.toInt(), segs[2].argbColor)
    }
}

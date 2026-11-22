package com.nana.samp.ui

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.Typeface
import android.util.AttributeSet
import android.view.MotionEvent
import android.view.View
import com.nana.samp.core.NativeBridge
import kotlin.math.abs
import kotlin.math.max

/**
 * Authentic SA-MP PC 640x480 TextDraw Canvas Renderer & Touch Click Hit-Tester.
 *
 * Renders Global TextDraws (0..2047) and Player TextDraws (2048..2303) sent by SA-MP/open.mp servers:
 * - Virtual 640x480 -> device screen scaling
 * - UseBox background rectangles (Left / Center / Right alignment)
 * - GTA SA inline color tags (~r~, ~g~, ~b~, ~w~, ~y~, ~p~, ~l~, ~h~, ~n~ newline) & {RRGGBB}
 * - Outline & Drop Shadow
 * - Font styles 0..3, sprite (4), and 3D preview model placeholder badge (5)
 * - Selectable TextDraw touch hit-testing when RPC_SelectTextDraw (83) is active
 */
class TextDrawOverlayView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
) : View(context, attrs) {

    var onClickTextDraw: ((Int) -> Unit)? = null

    private var textDraws: List<NativeBridge.TextDrawModel> = emptyList()
    private var selecting: Boolean = false
    private var hoverColorArgb: Int = Color.YELLOW
    private var pressedId: Int = -1

    private val hitBoxes = ArrayList<Pair<Int, RectF>>()
    private val boxPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.FILL }
    private val borderPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = 2.5f
    }
    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { isSubpixelText = true }
    private val shadowPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { isSubpixelText = true }

    fun updateState(state: NativeBridge.TextDrawOverlayState) {
        textDraws = state.items
        selecting = state.selecting
        hoverColorArgb = ChatColorParser.sampRgbaToArgb(state.hoverRgba)
        invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        hitBoxes.clear()
        if (width <= 0 || height <= 0 || textDraws.isEmpty()) return

        val sx = width / 640f
        val sy = height / 480f

        for (td in textDraws) {
            drawSingleTextDraw(canvas, td, sx, sy)
        }
    }

    private fun drawSingleTextDraw(
        canvas: Canvas,
        td: NativeBridge.TextDrawModel,
        sx: Float,
        sy: Float,
    ) {
        val px = td.x * sx
        val py = td.y * sy

        // Choose font typeface matching SA-MP PC styles 0..3
        textPaint.typeface = when (td.style) {
            0 -> Typeface.create(Typeface.SERIF, Typeface.BOLD_ITALIC)
            2 -> Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)
            3 -> Typeface.create(Typeface.DEFAULT_BOLD, Typeface.BOLD)
            else -> Typeface.create(Typeface.SANS_SERIF, Typeface.NORMAL)
        }
        val textPx = (max(0.25f, td.letterHeight) * 10.5f * sy).coerceIn(10f, height * 0.25f)
        textPaint.textSize = textPx
        textPaint.textScaleX = (max(0.18f, td.letterWidth) / max(0.25f, td.letterHeight) * 2.15f)
            .coerceIn(0.55f, 2.2f)

        val displayText = when {
            td.style == 5 && td.modelId > 0 -> td.text.ifBlank { "[Model #${td.modelId}]" }
            td.style == 4 -> if (td.text.contains(':')) "" else td.text
            else -> td.text
        }
        val lines = splitLines(displayText)
        val lineStep = textPx * 1.18f
        val measuredMaxW = lines.maxOfOrNull { line ->
            textPaint.measureText(stripTags(line))
        } ?: 0f
        val measuredH = max(lineStep, lines.size * lineStep)

        // Compute box / clickable bounds in screen coordinates (SA-MP PC rules)
        val boxRect: RectF = when {
            td.style == 4 || td.style == 5 -> {
                val bw = if (abs(td.lineWidth) > 1f) abs(td.lineWidth) * sx else 48f * sx
                val bh = if (abs(td.lineHeight) > 1f) abs(td.lineHeight) * sy else 48f * sy
                RectF(px, py, px + bw, py + bh)
            }
            td.alignment == 2 -> { // Center: lineHeight = box width in SA-MP!
                val bw = if (td.lineHeight > 1f && td.lineHeight < 640f) td.lineHeight * sx else max(measuredMaxW + 12f, 24f * sx)
                val bh = if (td.lineWidth > 1f && td.lineWidth < 480f && td.useBox) max(measuredH, td.lineWidth * sy * 0.1f) else measuredH + 6f
                RectF(px - bw * 0.5f, py - 2f, px + bw * 0.5f, py + bh)
            }
            td.alignment == 3 -> { // Right
                val left = if (td.lineWidth in 1f..640f && td.lineWidth < td.x) td.lineWidth * sx else px - max(measuredMaxW + 10f, 24f * sx)
                RectF(left, py - 2f, px + 4f, py + measuredH + 6f)
            }
            else -> { // Left (default): lineWidth is right-edge X in 640 space when useBox
                val right = if (td.lineWidth > td.x && td.lineWidth <= 640f) {
                    td.lineWidth * sx
                } else {
                    px + max(measuredMaxW + 10f, 24f * sx)
                }
                val bottom = if (td.useBox && td.lineHeight > 1f && td.lineHeight < 480f) {
                    max(py + measuredH + 4f, py + td.lineHeight * sy)
                } else {
                    py + measuredH + 6f
                }
                RectF(px - 4f, py - 2f, right, bottom)
            }
        }

        if (td.selectable) {
            // Add slight touch padding for finger accuracy on mobile screens
            val touchRect = RectF(boxRect.left - 8f, boxRect.top - 8f, boxRect.right + 8f, boxRect.bottom + 8f)
            hitBoxes.add(Pair(td.id, touchRect))
        }

        // 1. Box / Sprite / Preview Model background
        if (td.useBox || td.style == 4 || td.style == 5) {
            val fillArgb = when {
                td.style == 4 -> abgrToArgb(td.letterColorAbgr)
                td.style == 5 -> abgrToArgb(td.bgColorAbgr)
                else -> abgrToArgb(td.boxColorAbgr)
            }
            boxPaint.color = fillArgb
            canvas.drawRect(boxRect, boxPaint)
        }

        // Highlight selectable TextDraws when server activates cursor selection mode
        if (selecting && td.selectable) {
            borderPaint.color = if (pressedId == td.id) hoverColorArgb else Color.argb(170, 56, 189, 248)
            canvas.drawRect(boxRect, borderPaint)
        }

        // 2. Render text lines with GTA ~color~ tags, shadow & outline
        if (displayText.isNotBlank() && displayText != "_" && displayText != " ") {
            val baseLetterArgb = if (selecting && pressedId == td.id) {
                hoverColorArgb
            } else {
                abgrToArgb(td.letterColorAbgr)
            }
            val bgArgb = abgrToArgb(td.bgColorAbgr)
            shadowPaint.set(textPaint)
            shadowPaint.color = if (Color.alpha(bgArgb) > 10) bgArgb else Color.argb(200, 0, 0, 0)

            var curY = py + textPx * 0.92f
            for (line in lines) {
                val segments = parseGtaSegments(line, baseLetterArgb)
                val totalLineW = segments.sumOf { textPaint.measureText(it.text).toDouble() }.toFloat()
                var drawX = when (td.alignment) {
                    2 -> px - totalLineW * 0.5f
                    3 -> px - totalLineW
                    else -> px
                }
                for (seg in segments) {
                    if (td.outline > 0) {
                        val o = (td.outline * 1.1f).coerceAtMost(4f)
                        canvas.drawText(seg.text, drawX - o, curY, shadowPaint)
                        canvas.drawText(seg.text, drawX + o, curY, shadowPaint)
                        canvas.drawText(seg.text, drawX, curY - o, shadowPaint)
                        canvas.drawText(seg.text, drawX, curY + o, shadowPaint)
                    } else if (td.shadow > 0) {
                        val s = (td.shadow * 1.2f).coerceAtMost(4f)
                        canvas.drawText(seg.text, drawX + s, curY + s, shadowPaint)
                    }
                    textPaint.color = seg.argbColor
                    canvas.drawText(seg.text, drawX, curY, textPaint)
                    drawX += textPaint.measureText(seg.text)
                }
                curY += lineStep
            }
        }
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        if (!selecting || hitBoxes.isEmpty()) return false
        val x = event.x
        val y = event.y
        val hit = hitBoxes.lastOrNull { (_, rect) -> rect.contains(x, y) }

        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                if (hit != null) {
                    pressedId = hit.first
                    invalidate()
                    return true
                }
                return false
            }
            MotionEvent.ACTION_UP -> {
                val targetId = pressedId
                pressedId = -1
                invalidate()
                if (hit != null && hit.first == targetId) {
                    onClickTextDraw?.invoke(targetId)
                    return true
                }
                return false
            }
            MotionEvent.ACTION_CANCEL -> {
                pressedId = -1
                invalidate()
                return false
            }
        }
        return pressedId >= 0
    }

    companion object {
        fun abgrToArgb(abgr: Int): Int {
            val r = abgr and 0xFF
            val g = (abgr ushr 8) and 0xFF
            val b = (abgr ushr 16) and 0xFF
            val a = (abgr ushr 24) and 0xFF
            return (a shl 24) or (r shl 16) or (g shl 8) or b
        }

        fun splitLines(raw: String): List<String> =
            raw.replace("~N~", "~n~").split("~n~", "\n")

        fun stripTags(line: String): String =
            line.replace(Regex("~[a-zA-Z]~"), "").replace(Regex("\\{[0-9A-Fa-f]{6}\\}"), "")

        fun parseGtaSegments(line: String, baseArgb: Int): List<ChatColorParser.Segment> {
            val out = mutableListOf<ChatColorParser.Segment>()
            var curColor = baseArgb
            val buf = StringBuilder()
            var i = 0
            while (i < line.length) {
                if (line[i] == '~' && i + 2 < line.length && line[i + 2] == '~') {
                    if (buf.isNotEmpty()) {
                        out.add(ChatColorParser.Segment(buf.toString(), curColor))
                        buf.clear()
                    }
                    curColor = when (line[i + 1].lowercaseChar()) {
                        'r' -> Color.rgb(220, 48, 48)
                        'g' -> Color.rgb(54, 196, 86)
                        'b' -> Color.rgb(66, 135, 245)
                        'y' -> Color.rgb(245, 212, 66)
                        'p' -> Color.rgb(186, 104, 200)
                        'l' -> Color.rgb(18, 18, 18)
                        'w', 's' -> Color.rgb(240, 240, 240)
                        'h' -> brighten(curColor)
                        else -> curColor
                    }
                    i += 3
                } else if (line[i] == '{' && i + 7 < line.length && line[i + 7] == '}') {
                    val hex = line.substring(i + 1, i + 7)
                    val parsed = hex.toIntOrNull(16)
                    if (parsed != null) {
                        if (buf.isNotEmpty()) {
                            out.add(ChatColorParser.Segment(buf.toString(), curColor))
                            buf.clear()
                        }
                        curColor = (0xFF shl 24) or parsed
                        i += 8
                    } else {
                        buf.append(line[i])
                        i++
                    }
                } else {
                    buf.append(line[i])
                    i++
                }
            }
            if (buf.isNotEmpty()) {
                out.add(ChatColorParser.Segment(buf.toString(), curColor))
            }
            return out
        }

        private fun brighten(argb: Int): Int {
            val a = Color.alpha(argb)
            val r = (Color.red(argb) + 55).coerceAtMost(255)
            val g = (Color.green(argb) + 55).coerceAtMost(255)
            val b = (Color.blue(argb) + 55).coerceAtMost(255)
            return Color.argb(a, r, g, b)
        }
    }
}

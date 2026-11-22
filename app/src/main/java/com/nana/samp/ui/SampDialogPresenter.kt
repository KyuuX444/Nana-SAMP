package com.nana.samp.ui

import android.app.Activity
import android.app.Dialog
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.ColorDrawable
import android.graphics.drawable.GradientDrawable
import android.text.InputType
import android.text.SpannableStringBuilder
import android.text.Spanned
import android.text.style.ForegroundColorSpan
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.Window
import android.view.inputmethod.EditorInfo
import android.widget.BaseAdapter
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ListView
import android.widget.ScrollView
import android.widget.TextView
import com.nana.samp.core.NativeBridge

/**
 * Authentic SA-MP PC DXUT-Style Native Dialog Presenter (no MonetLoader required).
 *
 * Replicates the classic SA-MP PC look & UX natively on ARM64/ARMv7:
 * - Dark translucent bordered window (#DD0A0E17 + #557799 1.5dp border)
 * - Classic PC title header bar with {RRGGBB} colors
 * - True tabular column layout for DIALOG_STYLE_TABLIST & DIALOG_STYLE_TABLIST_HEADERS
 * - Classic SA-MP PC crimson active row highlight (#B91C1C) + double-tap quick submit
 * - Authentic PC bottom button bar ([Button 1] [Button 2])
 *
 * Supports all 6 SA-MP dialog styles:
 *   0 = MSGBOX, 1 = INPUT, 2 = LIST, 3 = PASSWORD, 4 = TABLIST, 5 = TABLIST_HEADERS
 */
class SampDialogPresenter(
    private val activity: Activity,
    private val onRespond: (dialogId: Int, button: Int, listItem: Int, inputText: String) -> Unit,
) {
    private var activeDialog: Dialog? = null
    private var shownDialogId: Int = -1

    fun sync(model: NativeBridge.DialogModel?) {
        if (model == null) {
            dismiss()
            return
        }
        if (shownDialogId == model.id && activeDialog?.isShowing == true) {
            return
        }
        dismiss()
        shownDialogId = model.id
        show(model)
    }

    fun dismiss() {
        shownDialogId = -1
        try {
            activeDialog?.dismiss()
        } catch (_: Exception) {
        }
        activeDialog = null
    }

    private fun dp(v: Int): Int = (v * activity.resources.displayMetrics.density).toInt()

    private fun coloredSpan(raw: String, baseRgba: Int = 0xFFFFFFFF.toInt()): CharSequence {
        val sb = SpannableStringBuilder()
        for (seg in ChatColorParser.parse(raw, baseRgba)) {
            val start = sb.length
            sb.append(seg.text)
            sb.setSpan(
                ForegroundColorSpan(seg.argbColor),
                start,
                sb.length,
                Spanned.SPAN_EXCLUSIVE_EXCLUSIVE,
            )
        }
        return sb
    }

    private fun makePcPanelBg(fillColor: Int, strokeColor: Int, radiusDp: Int): GradientDrawable =
        GradientDrawable().apply {
            setColor(fillColor)
            setStroke(dp(1), strokeColor)
            cornerRadius = dp(radiusDp).toFloat()
        }

    private fun show(model: NativeBridge.DialogModel) {
        val dlg = Dialog(activity)
        dlg.requestWindowFeature(Window.FEATURE_NO_TITLE)
        dlg.setCancelable(false)
        dlg.window?.setBackgroundDrawable(ColorDrawable(Color.TRANSPARENT))

        val root = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            background = makePcPanelBg(Color.parseColor("#E60B101B"), Color.parseColor("#64748B"), 6)
            setPadding(dp(12), dp(10), dp(12), dp(10))
        }

        // 1. Classic SA-MP PC Caption Bar
        val caption = TextView(activity).apply {
            text = coloredSpan(model.title.ifBlank { "SA-MP" })
            textSize = 14f
            setTypeface(Typeface.SANS_SERIF, Typeface.BOLD)
            setTextColor(Color.WHITE)
            background = makePcPanelBg(Color.parseColor("#F0141C2B"), Color.parseColor("#334155"), 4)
            setPadding(dp(10), dp(7), dp(10), dp(7))
        }
        root.addView(
            caption,
            LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT),
        )

        var inputField: EditText? = null
        var selectedIdx = 0
        var listRows: List<String> = emptyList()

        fun submit(button: Int) {
            val text = when (model.style) {
                1, 3 -> inputField?.text?.toString() ?: ""
                2, 4, 5 -> listRows.getOrNull(selectedIdx)?.replace(Regex("\\{[0-9A-Fa-f]{6}\\}"), "") ?: ""
                else -> ""
            }
            val itemIdx = if (model.style in listOf(2, 4, 5)) selectedIdx else -1
            shownDialogId = -1
            runCatching { dlg.dismiss() }
            activeDialog = null
            onRespond(model.id, button, itemIdx, text)
        }

        // 2. Dialog Body
        val bodyContainer = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(4), dp(10), dp(4), dp(10))
        }

        when (model.style) {
            0 -> { // MSGBOX
                val tv = TextView(activity).apply {
                    text = coloredSpan(model.info)
                    textSize = 13f
                    setTextColor(Color.parseColor("#E2E8F0"))
                    setLineSpacing(dp(2).toFloat(), 1.0f)
                }
                val scroll = ScrollView(activity).apply {
                    addView(tv)
                    layoutParams = LinearLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        dp(190),
                    )
                }
                bodyContainer.addView(scroll)
            }
            1, 3 -> { // INPUT / PASSWORD
                val tv = TextView(activity).apply {
                    text = coloredSpan(model.info)
                    textSize = 13f
                    setTextColor(Color.parseColor("#E2E8F0"))
                    setPadding(0, 0, 0, dp(10))
                }
                val edit = EditText(activity).apply {
                    setTextColor(Color.WHITE)
                    setHintTextColor(Color.parseColor("#94A3B8"))
                    textSize = 13f
                    background = makePcPanelBg(Color.parseColor("#EE050810"), Color.parseColor("#94A3B8"), 4)
                    setPadding(dp(10), dp(8), dp(10), dp(8))
                    isSingleLine = true
                    imeOptions = EditorInfo.IME_ACTION_DONE
                    hint = if (model.style == 3) "Password..." else "Type here..."
                    inputType = if (model.style == 3) {
                        InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_PASSWORD
                    } else {
                        InputType.TYPE_CLASS_TEXT
                    }
                    setOnEditorActionListener { _, actionId, _ ->
                        if (actionId == EditorInfo.IME_ACTION_DONE) {
                            submit(1)
                            true
                        } else {
                            false
                        }
                    }
                }
                inputField = edit
                val scrollPrompt = ScrollView(activity).apply {
                    addView(tv)
                    layoutParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(95))
                }
                bodyContainer.addView(scrollPrompt)
                bodyContainer.addView(
                    edit,
                    LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT),
                )
            }
            2, 4, 5 -> { // LIST / TABLIST / TABLIST_HEADERS
                val rawLines = model.info.replace("\r\n", "\n").split('\n').filter { it.isNotEmpty() }
                val hasHeader = (model.style == 5 && rawLines.size > 1)
                val isTabular = (model.style == 4 || model.style == 5)
                if (hasHeader) {
                    val hdrRow = buildTabRowView(rawLines.first(), isTabular, isHeader = true, isSelected = false)
                    bodyContainer.addView(hdrRow)
                }
                listRows = if (hasHeader) rawLines.drop(1) else rawLines

                var lastClickedPos = -1
                var lastClickTime = 0L

                val listAdapter = object : BaseAdapter() {
                    override fun getCount(): Int = listRows.size
                    override fun getItem(position: Int): Any = listRows[position]
                    override fun getItemId(position: Int): Long = position.toLong()
                    override fun getView(position: Int, convertView: View?, parent: ViewGroup?): View {
                        return buildTabRowView(
                            rowText = listRows[position],
                            isTabular = isTabular,
                            isHeader = false,
                            isSelected = (position == selectedIdx),
                        )
                    }
                }

                val listView = ListView(activity).apply {
                    adapter = listAdapter
                    divider = ColorDrawable(Color.parseColor("#334155"))
                    dividerHeight = 1
                    background = makePcPanelBg(Color.parseColor("#CC050810"), Color.parseColor("#334155"), 4)
                    setOnItemClickListener { _, _, position, _ ->
                        val now = System.currentTimeMillis()
                        if (position == lastClickedPos && (now - lastClickTime) < 420L) {
                            selectedIdx = position
                            submit(1)
                        } else {
                            selectedIdx = position
                            lastClickedPos = position
                            lastClickTime = now
                            listAdapter.notifyDataSetChanged()
                        }
                    }
                    layoutParams = LinearLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        dp(200),
                    )
                }
                bodyContainer.addView(listView)
            }
            else -> {
                val tv = TextView(activity).apply {
                    text = coloredSpan(model.info)
                    setTextColor(Color.WHITE)
                }
                bodyContainer.addView(tv)
            }
        }

        root.addView(
            bodyContainer,
            LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT),
        )

        // 3. Classic SA-MP PC Bottom Button Row
        val btnBar = LinearLayout(activity).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER
            setPadding(0, dp(4), 0, 0)
        }

        fun makePcButton(label: String, onClick: () -> Unit): Button = Button(activity).apply {
            text = coloredSpan(label)
            textSize = 12f
            isAllCaps = false
            setTextColor(Color.WHITE)
            background = makePcPanelBg(Color.parseColor("#EE1E293B"), Color.parseColor("#94A3B8"), 4)
            minWidth = dp(104)
            minHeight = dp(34)
            setPadding(dp(16), dp(6), dp(16), dp(6))
            setOnClickListener { onClick() }
        }

        val btn1 = makePcButton(model.button1.ifBlank { "OK" }) { submit(1) }
        btnBar.addView(btn1)

        if (model.button2.isNotBlank()) {
            val btn2 = makePcButton(model.button2) { submit(0) }
            val lp = LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply {
                marginStart = dp(12)
            }
            btnBar.addView(btn2, lp)
        }

        root.addView(
            btnBar,
            LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT),
        )

        dlg.setContentView(root)
        val screenW = activity.resources.displayMetrics.widthPixels
        dlg.window?.setLayout((screenW * 0.68f).toInt().coerceAtMost(dp(560)), ViewGroup.LayoutParams.WRAP_CONTENT)
        dlg.show()
        activeDialog = dlg
    }

    private fun buildTabRowView(
        rowText: String,
        isTabular: Boolean,
        isHeader: Boolean,
        isSelected: Boolean,
    ): View {
        val row = LinearLayout(activity).apply {
            orientation = LinearLayout.HORIZONTAL
            setPadding(dp(8), dp(6), dp(8), dp(6))
            setBackgroundColor(
                when {
                    isHeader -> Color.parseColor("#DD1E293B")
                    isSelected -> Color.parseColor("#CC991B1B") // Classic SA-MP PC crimson highlight
                    else -> Color.TRANSPARENT
                }
            )
        }
        val cols = if (isTabular) rowText.split('\t') else listOf(rowText)
        for (col in cols) {
            val tv = TextView(activity).apply {
                text = coloredSpan(col)
                textSize = if (isHeader) 12f else 12.5f
                setTextColor(if (isHeader) Color.parseColor("#38BDF8") else Color.WHITE)
                if (isHeader) setTypeface(Typeface.SANS_SERIF, Typeface.BOLD)
                setSingleLine(false)
            }
            val lp = LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f).apply {
                marginEnd = dp(6)
            }
            row.addView(tv, lp)
        }
        return row
    }
}
